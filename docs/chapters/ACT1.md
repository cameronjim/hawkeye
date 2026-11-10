# Act 1: Night Shift, as chapters

The beta is Act 1 of `docs/STORY.md` (decided with Cameron, 2026-11-09). This maps his
beats onto chapters and onto systems that exist. The beats are his; the split into
chapters, the order of unlocks, and anything marked *proposal* are Claude's and are his
to change. All dialogue in the build is a draft until he edits it.

Setting: early December, one year after the show. Christmas lights up, snow on the
ground. One district: the East Village and Lower East Side block around Tompkins Square
Park. Playable: Kate. Clint is a voice on the phone, never on screen as a partner.
Lucky follows Kate.

## Chapters

| # | Chapter | Story beat | Teaches | Built on |
|---|---------|------------|---------|----------|
| 1 | **Night Shift** | Mid-chase after a bike thief across the rooftops, Kate narrating to Lucky. A botched grapple drops her in a dumpster of wrapping paper. She still catches him. | Sprint, vault, mantle, grapple, chain, the takedown | The district, traversal, the robbery crime's runner |
| 2 | **Reindeer Games** | A toy drive robbed by a crew in reindeer masks. | Melee chain, parry, finisher, the bow in a fight. Unlocks the **sticky foam** arrow | Street crimes, thug squads, the putty arrow renamed |
| 3 | **The Grotto** | A mall Santa running a fencing operation out of his grotto. The stolen sleigh display turns up inside (*proposal*: the sleigh theft is this chapter's hook, not its own mission). | Stealth, takedowns from above, interiors. Unlocks the **boomerang** arrow | The interior generator, interior enemies, the vault door |
| 4 | **The Social Club** | Tomas, on the roof above the closed social club, with his guys, a speaker and an inflatable Santa. He's sorry about it. Ends with the cruiser, the wave, the warning about the Sloan money, and the city lit up. | The whole loop under pressure: dodge, arrow mix-ups, trick-arrow finishers. Unlocks the **confetti** arrow | Boss phases, melee, parry, finishers |

Free roam sits between chapters, with crimes and challenges as now.

## Threads that run alongside

| Thread | Where it shows | Built on |
|--------|----------------|----------|
| Clint texting "take a night off"; a video call from the farm with Laura and the kids heckling | Phone, after chapters 1 and 3 | Phone and dialogue systems |
| Rent three weeks late, the landlord, job hunting | Phone texts and the safehouse door | Phone, safehouse |
| "Hawkeye's sidekick" in the press | Phone news items, thug barks | Phone, banter |
| Visiting Eleanor in prison | A slideshow after chapter 2 (*proposal*: slides, not a playable scene) | Flashback slideshow |
| The dead courier with one arrow-sized wound | An examine on a street corner in chapter 2 | Examine interactable |
| The target-shaped ornament on Kate's fire escape | At the safehouse after chapter 3 | Examine interactable |
| Petty crimes circling the same few addresses | Crime spots clustered on the world map | Crimes, world map |

## The Tomas fight

Built 2026-11-10 as chapter 4 on the boss framework (`CH04.md`; rules in `claude-docs/gameplay-semantics.md`,
"Bosses"). It follows chapter 2 in the campaign's order until chapter 3 exists.

## Chapter 2, Reindeer Games
 2026-11-10 (`CH02.md`): the toy drive on Avenue B as a chapter site, the reindeer crew, the sticky foam
(the putty arrow, shown as "Foam") unlocked on arrival, the courier as an optional unmarked examine raising
`saw_courier`, the "Visiting hours" slides and the phone thread after.

Three phases on the boss phase system. A formality, and funny.

1. **His guys.** Four to six thugs with oversized holiday props (candy-cane bat, a
   wrapped-present shield, a string of lights as a whip). Tomas watches and comments.
2. **Tomas.** A decent brawler with long telegraphs. He stops to compliment her form
   and complain about his knees, which is the opening for a finisher.
3. **The inflatable Santa.** It comes loose and collapses across the arena, changing
   the space; Tomas and whoever is left fight in what remains.

## What the story removes from the current build

- The purple-fletched arrow ending of the old chapter 1, its slides and the grey room.
- Clint as an on-screen partner and the switch key, for Act 1. The code stays for Act 2.
- The archer duel as a required encounter. Archers are Barney's people and belong to Act 2;
  rooftop crimes may keep one.
- January. The district becomes early December.

## New things the story needs

| Need | Size |
|------|------|
| Lucky as a follower in a sweater | Medium. No dog mesh or animation on disk; placeholder until an asset is chosen |
| Boomerang arrow (returns along its path, can hit on the way back, Kate included) | Small |
| Confetti arrow (a burst that blinds briefly; she insists it is tactical) | Small. Built 2026-11-10 as data on the smoke arrow (`DA_Arrow_Confetti`) |
| Sticky foam (the putty arrow renamed; also sticks to walls as a foothold, *proposal*) | Small |
| Holiday prop weapons and reindeer masks for thugs | Small, script-built |
| Social club rooftop arena: string lights, speaker, inflatable Santa | Medium, script-built on a real roof. Built 2026-11-10 (`CH04.md`) |
| December dressing: lights on fire escapes, wreaths, trees for sale, lit windows | Medium, in `generate_city.py` |
| The scripted dumpster landing | Small |
