"""Picks the gate steps a change needs. Pure Python, no engine; Tools\\gate.ps1 runs the plan.

    git diff --name-only <base>..HEAD | python -I -B Tools/Gate/select.py [--rules rules.json] [--timings t.json] [--repo .]

Reads changed paths on stdin (one per line) and prints a JSON plan on stdout. The rules are data
(Tools/Gate/rules.json); the plan is the union over every rule that matches a changed path:

  base rule      always applies to what it matches (every Tools file: python tests and ruff)
  fallback rule  applies to a path only when no other non-base rule matched it
  no rule        the path is unknown: the plan goes broad (build, the whole suite, python)

Run it with -I (or from another folder): this file is called select.py, and Python's script folder would
otherwise shadow the standard library's select module for anything it imports.
"""

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RULES = os.path.join(HERE, "rules.json")

_TEST_NAME = re.compile(r'AUTOMATION_TEST\w*\(\s*\w+\s*,\s*"([^"]+)"')


def glob_to_regex(glob):
    """** crosses folders, * and ? stay inside one. Matched against the whole path, case-insensitively."""
    out = []
    i = 0
    while i < len(glob):
        ch = glob[i]
        if ch == "*":
            if glob[i:i + 3] == "**/":
                out.append("(?:.*/)?")
                i += 3
                continue
            if glob[i:i + 2] == "**":
                out.append(".*")
                i += 2
                continue
            out.append("[^/]*")
        elif ch == "?":
            out.append("[^/]")
        else:
            out.append(re.escape(ch))
        i += 1
    return re.compile("^" + "".join(out) + "$", re.IGNORECASE)


def normalise(path):
    path = path.strip().replace("\\", "/")
    while path.startswith("./"):
        path = path[2:]
    return path


def load_rules(path=DEFAULT_RULES):
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    for rule in data["rules"]:
        rule["_match"] = [glob_to_regex(g) for g in rule.get("match", [])]
        rule["_exclude"] = [glob_to_regex(g) for g in rule.get("exclude", [])]
    return data


def rule_matches(rule, path):
    if any(rx.match(path) for rx in rule["_exclude"]):
        return False
    return any(rx.match(path) for rx in rule["_match"])


def test_names_in(text):
    return _TEST_NAME.findall(text)


def is_standalone_test(name, data):
    return any(name.startswith(prefix) for prefix in data.get("standaloneTestPrefixes", []))


def _unique(items):
    seen = []
    for item in items:
        if item not in seen:
            seen.append(item)
    return seen


def drop_subsumed(filters):
    """The engine matches a filter as a substring of the test's path, so 'Hawkeye.Ledge' already holds
    'Hawkeye.Ledge.Sequences': keep only filters that no other filter is a substring of."""
    return [f for f in filters if not any(g != f and g in f for g in filters)]


def select(paths, data, read_file=None):
    """The plan for these changed paths. read_file(path) -> text or None, for rules that read a test file."""
    paths = _unique([normalise(p) for p in paths if p.strip()])
    plan = {
        "changed": paths, "matched": [], "build": False, "tests": [], "wholeSuite": False, "python": False, "pythonSuites": [],
        "content": False, "contentSteps": [], "contentVerify": False, "cityVerify": False, "audit": False,
        "laps": [], "strips": [], "risky": False, "broad": False, "empty": False, "riskyRules": [],
    }
    full_laps = data.get("fullLaps", [])

    def apply(rule, path):
        for key in ("build", "python", "content", "contentVerify", "cityVerify", "audit", "risky"):
            if rule.get(key):
                plan[key] = True
        if rule.get("risky") and rule["name"] not in plan["riskyRules"]:
            plan["riskyRules"].append(rule["name"])
        if rule.get("broad"):
            plan["broad"] = True
        plan["pythonSuites"].extend(rule.get("pythonSuites", []))
        plan["tests"].extend(rule.get("tests", []))
        plan["laps"].extend(rule.get("laps", []))
        plan["strips"].extend(rule.get("strips", []))
        plan["contentSteps"].extend(rule.get("contentSteps", []) or (["all"] if rule.get("content") else []))
        if rule.get("testsFromFile"):
            text = read_file(path) if read_file else None
            names = test_names_in(text) if text else []
            if not names:
                plan["broad"] = True  # a test helper with no tests of its own: anything may use it
            for name in names:
                if name.startswith("Hawkeye.Lap."):
                    lap = name[len("Hawkeye.Lap."):]
                    if lap in full_laps:
                        plan["laps"].append(lap)
                elif not is_standalone_test(name, data):
                    plan["tests"].append(name)

    for path in paths:
        hits = [r for r in data["rules"] if rule_matches(r, path)]
        if not hits:
            plan["broad"] = True
            plan["build"] = True
            plan["matched"].append({"path": path, "rules": ["(no rule: broad)"]})
            continue
        specific = [r for r in hits if not r.get("base") and not r.get("fallback")]
        applied = [r for r in hits if r.get("base")] + specific
        if not specific:
            applied += [r for r in hits if r.get("fallback") and not r.get("base")]
        for rule in applied:
            apply(rule, path)
        plan["matched"].append({"path": path, "rules": [r["name"] for r in applied]})

    if plan["broad"]:
        plan["wholeSuite"] = True
        plan["python"] = True
        plan["build"] = True
        plan["pythonSuites"] = list(data.get("pythonSuites", []))
    plan["tests"] = drop_subsumed(_unique(plan["tests"]))
    plan["laps"] = _unique(plan["laps"])
    plan["strips"] = _unique(plan["strips"])
    plan["pythonSuites"] = [d for d in data.get("pythonSuites", []) if d in plan["pythonSuites"]]
    plan["contentSteps"] = _unique(plan["contentSteps"])
    if plan["content"] and "all" in plan["contentSteps"]:
        plan["contentSteps"] = ["all"]
    if plan["wholeSuite"]:
        plan["tests"] = []  # the suite already holds them
    plan["empty"] = not any((plan["build"], plan["tests"], plan["wholeSuite"], plan["python"], plan["content"],
                             plan["contentVerify"], plan["cityVerify"], plan["audit"], plan["laps"], plan["strips"]))
    steps = plan_steps(plan, data)
    plan["steps"] = steps
    plan["estimateSeconds"] = sum(s["seconds"] for s in steps)
    return plan


def plan_steps(plan, data, timings=None):
    """The ordered steps of a quick-tier plan with an estimate in seconds each. A measured time from an earlier
    run (timings, by step id) wins over the rule file's estimate."""
    est = data.get("estimates", {})
    timings = timings or {}
    steps = []

    def add(step_id, label, seconds):
        steps.append({"id": step_id, "label": label, "seconds": int(timings.get(step_id, seconds))})

    if plan["python"]:
        suites = plan["pythonSuites"]
        add("python:" + ",".join(suites), "python tests (" + ", ".join(suites) + ") and ruff" if suites else "ruff",
            est["python"] + sum(est.get("pythonSuite:" + d, 0) for d in suites))
    if plan["build"]:
        add("build", "build HawkeyeEditor", est["build"])
    if plan["wholeSuite"]:
        add("suite", "automation suite (whole)", est["suite"])
    elif plan["tests"]:
        add("tests", "automation: " + str(len(plan["tests"])) + " filters, one process",
            est["testsProcess"] + est["testsPerFilter"] * len(plan["tests"]))
    if plan["content"]:
        if plan["contentSteps"] == ["all"] or not plan["contentSteps"]:
            add("content", "create-content (all steps)", est["content"])
        else:
            add("content", "create-content: " + ", ".join(plan["contentSteps"]),
                est["testsProcess"] + est["contentStep"] * len(plan["contentSteps"]))
    if plan["contentVerify"]:
        add("contentVerify", "verify_content", est["contentVerify"])
    if plan["cityVerify"]:
        add("cityVerify", "city generate and verify", est["cityVerify"])
    if plan["audit"]:
        add("audit", "traversal audit", est["audit"])
    if plan["strips"]:
        add("strips", "traversal strips: " + "+".join(plan["strips"]), est["strips"])
    for lap in plan["laps"]:
        add("lap:" + lap, "lap " + lap, est["lap"])
    return steps


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--rules", default=DEFAULT_RULES)
    parser.add_argument("--timings", default="", help="JSON of measured seconds per step id, from earlier runs")
    parser.add_argument("--repo", default=os.path.dirname(os.path.dirname(HERE)))
    args = parser.parse_args(argv)

    data = load_rules(args.rules)
    timings = {}
    if args.timings and os.path.exists(args.timings):
        try:
            with open(args.timings, encoding="utf-8-sig") as handle:
                timings = json.load(handle)
        except (OSError, ValueError):
            timings = {}

    def read_file(path):
        try:
            with open(os.path.join(args.repo, path), encoding="utf-8", errors="ignore") as handle:
                return handle.read()
        except OSError:
            return None

    plan = select(sys.stdin.read().splitlines(), data, read_file)
    plan["steps"] = plan_steps(plan, data, timings)
    plan["estimateSeconds"] = sum(s["seconds"] for s in plan["steps"])
    json.dump(plan, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
