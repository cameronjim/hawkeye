# Gameplay semantics

What each system promises. Tests in `Source/Hawkeye/Tests` assert these. If you change a
rule here, change the test and the code in the same commit.

**Pivot note (2026-09-25).** The game is third person with bows. The first-person
viewmodel, the player's hitscan pistol, the weapon slots and the prison's choice flags are
gone from the code, and so from this file. Every section below describes code that exists.
`UWeaponComponent` survives for two users only: the thugs' pistol and Kate's `Hands` punch.

## Third-person camera and look (built 2026-09-26)
- Spring arm behind the character: hip length 350, socket offset (0, 70, 60), FOV 90.
  Aim (right mouse) blends over 0.15 s to length 180, offset (0, 70, 65) (moved out past the bow arm on 2026-09-27), FOV 70. The
  70 cm lateral offset puts the character around 40% of screen width so the right side
  of the frame is open. Position lag 10, rotation lag 12. Sensitivity from the settings
  subsystem. `ComputeCameraTargets(bAiming)` is pure and tested.
- The arm probes on the Camera channel with a 12 cm radius and pulls in against walls,
  never inside geometry. Closer than 100 cm the character is hidden from the camera so
  the view isn't inside her shoulder.
- Fight camera (2026-09-29): with two or more alerted thugs near her (one within 9 m, the rest within 15 m; flat, 3 m up or
  down), the hip boom lengthens 70 cm and the lens tips down 4 degrees, over 0.5 s; back over 1 s
  when the fight thins out. See "Combat readability".
- Movement input rotates the character toward the input direction; while aiming she
  faces the camera yaw. Look input never rotates her while idle.

## Movement (built 2026-09-26; every number is a property on BP_Kate)
- Locomotion animation is motion matching from the Game Animation Sample (see
  infrastructure.md). Kate's speeds are currently set by the sample's character graph
  (run 500, stop 200, aimed strafe 180, plus its walk and sprint gaits chosen from the
  bridged `WantsToWalk` / `WantsToSprint` flags). The native speeds below apply to thugs
  and to any character not derived from the sample. Reconciling the two is a stage 2
  TODO once traversal feel is settled.
- Native speeds: walk 250, run 500, sprint 700, crouch 200. Gamepad: stick at 0.9 or
  more runs immediately, 0.4 or more runs after 0.2 s, lighter walks. Keyboard always
  runs; Shift sprints.
- Sprint toggle (2026-09-29, after "sprint on controller should be pressing the left joystick once
  and the character continues to sprint the whole time"). On a pad one L3 press turns sprint on
  (`FHawkeyeSprintToggle`); it stays on with the stick let go of L3, through turns and stops of up to
  0.6 s, and turns off on a second press, when the stick has sat in its dead zone for more than
  0.6 s (counted from the raw stick, so a zip or a vault with the stick held keeps it), on aim (a
  toggled sprint gives way to the aim; a held Shift still refuses it), on a bow draw, on a crouch (a
  crouch at speed slides, and the toggle ends when the slide does, so she comes up at a run), and
  when a menu opens (every pause-style screen, through `ApplyPauseInputMode`). Shift stays a hold.
  The Sprint row in Settings (Controls) reads "Hold on keys, toggle on pad" (the default), Hold or
  Toggle; Hold or Toggle apply to both devices. Which device pressed is the controller's last input
  (`IsUsingGamepad`). The toggle's on and off log at Log with the reason ("sprint toggle off (stick
  centred for 0.6 s)"). The sample's `WantsToSprint` is still fed from the sprint gait. The first-time hints show the
  sprint key once a campaign (HUD, "First-time hints").
- Jump height 90 cm, air control 0.3.
- Slide: crouch while sprinting; 0.7 s, speed eases 750 to 200, capsule half-height 50,
  restored after.
- Crouch keeps (2026-09-29, from the interior stealth lap). A crouch ends only on the player's input (a toggle
  press, or the release when crouch is held), a slide's start, a dodge, a parkour move, hang or zip (entering the
  traversal or the zip mode, or the sample's traversal in Flying; she comes out of it standing, the movement component no longer crouches her again after), a
  takedown or finisher (allowed; today both leave her crouched), the downed state (and the revive from it), or a
  real fall: one of at least 0.3 s that has dropped at least 30 cm from its top (`CrouchKeepFallSeconds`,
  `CrouchKeepFallHeight`). Stairs, landings,
  doorways and door open events, a one-frame flick to falling and a stair's lip keep it
  (`Hawkeye.Crouch.KeepsOnStairsAndThroughDoorways`). Every C++ stand-up goes through `EndCrouch(Why)`; every
  crouch that ends logs its reason at Log ("crouch ended: a fall"), and one nobody in C++ asked for (a Blueprint
  or engine `UnCrouch`, or the wish cleared behind the character) logs a warning with the caller
  (`GetUnaskedCrouchEndCount`).
- Landing from above 400 cm halves speed and dips the camera 30 cm for 0.3 s (a
  placeholder for the roll and stumble). Fall damage starts at 900 cm at 10% of max
  health, rises to a 60% cap at 2500 cm, and never kills: it stops at 1 health.
- `hawkeye.DebugMovement 1` draws state, speed, and fall height on the HUD. Landing
  height, fall damage, and slide start are logged at Log; sprint start and stop at Verbose.
  On a ledge and in a parkour move the line reads the traversal instead of the gait: "hang",
  "hang shimmy", or the move with how long it has run ("corner 0.21 s", "leap", "hop", "climb",
  "vault", "mantle", "ledge grab", "drop to hang", "traversal" for the sample's), at the speed the
  capsule really moves (smoothed over a tenth of a second) and, hanging, how far along the ledge
  (`Hawkeye.Movement.DebugLineReadsTheHang`). A zip reads "zip" and its speed, not the gait.

## HUD (third person)
- No crosshair. A 4 px reticle dot appears only while aiming and flashes white for 0.1 s
  on a hit. The hotbar is the quiver: seven boxes along the bottom, one per arrow slot, each
  with its key, short name and count ("Inventory and hotbar").
- Grapple marker: a green hollow diamond with the key under it means a press zips there, always
  (no use limit, and grapple arrows have no count); grey means it would not (an arrow still in
  flight); a smaller grey diamond marks an anchor in view whose line is blocked.
  Rules in the traversal section; the look per state is `UHawkeyeHudWidget::GetGrappleMarkerLook`.
- No minimap: the compass is the HUD. The world map is its own paused screen (M, or hold D-pad up); see
  "World map".
- First-time hints (built 2026-09-29, so a new tester learns the controls from the game). One line, lower
  centre, 12 px over the hotbar's top (it follows the hotbar's height and the HUD scale): white text on a
  0.55 black backing with each key as a cream keycap, fading in over 0.2 s and out over 0.2 s. The bottom
  centre is one stack: hotbar, the hint line's band (45 px at scale 1 times the HUD scale, reserved whether
  or not a hint is up), and the banter subtitle 8 px over the band's top, never lower than its own 150 px, so
  they never overlap and the subtitle never jumps when a hint comes or goes (at scale 1 the line's top used to
  touch the subtitle's bottom exactly; at 1.4 they overlapped). Subtitle bottoms: 150 px at 0.8, 158 at 1.0,
  204 at 1.4 (`Hawkeye.Hud.HintAndSubtitleStack`, the real HUD laid out headless). The subtitle wraps at a fixed width,
  0.55 of the HUD's (360 to 900 px; 704 at 1280), never at its box's own size: auto-wrap inside a box that sizes to its
  text wrapped some lines a word at a time (2026-11-10). The interaction prompt (centred under the reticle, 120 px of
  top padding) moves up when a subtitle's top would come within 12 px of its bottom, never above the top quarter
  (`UHawkeyeHudWidget::ComputePromptTopPadding`, `Hawkeye.Hud.SubtitleWrapAndPromptClear`). Rules in
  `Source/Hawkeye/UI/HawkeyeHints.h` (`UHawkeyeHintRules`, C++ data on the class default, no asset;
  `FHawkeyeHintQueue`, pure), sensed and saved by `UHawkeyeHintSubsystem`, drawn by `UHawkeyeHintWidget`.
  - Tokens resolve to the device touched last (`IsUsingGamepad`): {Sprint} Shift / L3, {Jump} Space / A,
    {Grapple} Q / RB, {Draw} LMB / RT, {Aim} RMB / LT, {Strike} V / X, {Dodge} and {Crouch} Ctrl / B,
    {Interact} E / Y, {Takedown} F / Y, {Phone} P / Hold D-pad Down, {Map} M / Hold D-pad Up, {Switch} X / LB,
    {Wheel} Tab / View, {Partner} the partner's name. The line re-resolves live when the device changes. The
    sprint hint reads "Press" where the Sprint setting makes the button a toggle on that device.
  - A hint arms when its trigger has held its hold time and it has shows left (one per campaign, counted
    in the save's `HintIds` / `HintShowCounts` the moment it comes up, like `NoticedPlaces`); it stays up
    until its action is done or 6 s after the trigger clears, and waits in the queue that long. One line
    only: the most relevant armed hint shows; a higher one takes the line once the shown one has had 1.5 s,
    or at once if urgent (parry, dodge). Doing a hint's action at any time, shown or not, counts it learned,
    so a player who already sprints never sees the sprint hint. Chapter beats (title card, close-up, the
    flow's end card and flashback), every menu and full screen (pause, settings, inventory, phone, map,
    safehouse, main menu, the quiver wheel, anything paused), the downed state and a challenge results card
    hide the line and freeze every clock, so a trigger that fires under a menu shows when it closes. Hints
    only read the game: nothing takes input, pauses or slows. Each show logs "Hint: sprint "Hold [Shift] to
    sprint"." at Log.
  - The set, by priority: parry ("[V] as the ! flashes to parry"; a thug within 3 m in a parryable wind-up),
    dodge ("Tap [Ctrl] to dodge"; the first health lost in a fight), finisher ("[F] to finish him"; a
    knocked-down or parry-staggered thug within 8 m with the finisher open), takedown ("[F] from behind to
    take him down"; a takedown is valid), strike ("[V] to strike, hold for heavy"; a standing thug within 3 m),
    draw ("[RMB] then [LMB], release to fire"; with a bow, a standing thug 8 to 40 m out within 30 degrees
    of the view and in sight), switch ("[X] to play as Clint"; the partner within 5 m in a fight, switching
    allowed), chain ("[Q] again mid-zip to chain"; mid-zip past the chain point with another anchor ready),
    grapple ("[Q] to zip"; the green diamond), vault ("[Space] or sprint into it to vault"; moving, a vault
    obstacle within 3 m ahead), climb ("[Space] at the wall to climb it"; a 150 to 260 cm wall within 2 m
    that a mantle or ledge grab takes), hang ("[Ctrl] at the edge to drop and hang"; at an edge with 3 m or
    more of drop), sprint ("Hold [Shift] to sprint"; 3 s moving on open ground, nothing within 3 m ahead,
    not sprinting, aiming or fighting), crime ("Follow the red ! to stop the crime"; a crime on, over 15 m
    away), wheel ("Hold [Tab] to pick a trick arrow"; a trick arrow comes into the quiver), phone ("[P] to
    read the text"; an unread text), safehouse ("[E] at the door to use the safehouse"; within 15 m),
    challenge ("[E] at the pedestal to start a challenge"; within 10 m), fast travel ("Fast travel from
    either safehouse's menu"; the second safehouse found), map ("[M] for the map"; 40 m from where the
    session started with no objective marker on screen, or a "nearby" toast). The finisher and takedown use
    {Takedown} (F) rather than {Interact} (E) because that is their key on a keyboard; on a pad both are Y.
  - Off under automation unless `hawkeye.Hints 2` (0 never, 1 the default: outside automation), so no hint
    lands in another pass's shot. Tests: `Hawkeye.Hints.*`; `hint_sprint.png`, `hint_grapple.png` and
    `hint_parry.png` in the Kate pass (`Hawkeye.Screenshot.Hints` on its own) check the line is up, faded in,
    centred on the lower half, over the hotbar and clear of the crime and challenge panels.

## Traversal (built 2026-09-26; moves and zip revised 2026-11-09 and 2026-11-10)
- Parkour (built 2026-09-26). Vault and mantle run through the Game Animation Sample's
  traversal (`AC_TraversalLogic` on the sample character, called by reflection from
  `GaspTraversal.cpp`); ledge grab, hang, climb, and drop are ours in `UParkourComponent`,
  which is also the fallback when the sample finds no traversable block or montage.
  Vault: obstacle top 60 to 110 cm with a back edge within 120 cm and a floor beyond that
  fits the capsule. Mantle: up to 200 cm with room to stand, or over a thin top such as a
  parapet onto a floor no more than 120 cm below. Ledge grab: 200 to 260 cm; hang with
  feet 145 cm below the edge and the capsule 36 cm off the wall; jump climbs, crouch
  drops (the stick adds shimmy, corners, hang jumps and the auto climb: "Hang" below). Falling past a ledge 150 to 330 cm above the feet catches it (260 while rising). Auto triggers
  within 120 cm while sprinting at 300 cm/s or more, and refuses a vault whose far floor
  is more than 150 cm down (the jump key allows up to 400), so sprinting at a parapet
  never throws the character off a roof. Durations: vault 0.5 s, mantle 0.8 s, jump to
  hang 0.35 s, catch 0.15 s, climb 1.0 s; input is locked only that long, except that the
  sample's montages hold input until they blend out.
- Traversal movement (revised 2026-11-10; the moves and hangs were `MOVE_Flying` with the capsule placed by
  hand every tick, velocity zero, so every move ended at a standstill). Our moves and hangs are a custom
  movement mode on `UHawkeyeMovementComponent` (`MOVE_Custom`, `EHawkeyeCustomMovement::Traversal`); the
  sample's own traversal still runs in Flying. `UParkourComponent` keeps the rules (the probe, the move, the
  roof-edge guard, the hang, shimmy and corners, the log, the buffered press) and hands the movement one
  `FHawkeyeTraversalMove`: a path (capsule centre and yaw at even steps over the move's time) or, for a
  clip-driven move, the clip's root motion; what the sweep may pass through; and how it ends. The capsule is
  swept every sub-step and `Velocity` is what it really moved over the time. Only the move's own obstacle is
  passed through (the block a vault or mantle climbs over, the ledge a climb goes over, the lip of a drop
  to hang and the floor she leaves, the wall a hang is on and the one a corner, leap or hop goes to), and
  only until the move ends; anything else stops her and she slides along it, the move still ending on time.
  A path move is where its path says at that time, so it ends in the same place at 30 and 120 Hz. A hang
  is the same mode holding still (velocity zero); a shimmy moves the held point and the velocity reads the
  shimmy's speed.
- Momentum out of a move (2026-11-10). A vault leaves in Walking (Falling with no floor under the landing)
  moving along the vault at her speed along it when it started, at least 300 cm/s (`VaultExitMinSpeed`) and
  at most 750 (`VaultExitMaxSpeed`, above the 700 sprint so a sprint keeps its speed); a vault takes 0.5 s, or
  less when that speed would cover its ground sooner, never under 0.35 s (`VaultMinSeconds`). A mantle and a
  climb leave in Walking moving 150 cm/s (`StepOnSpeed`) the way she faced the wall, so a stick held on runs
  straight on; the roof-edge guard's mantle onto a parapet stops there, with nothing toward the drop. The same exit speed is given when the sample's traversal ends, if she is slower: a sprint
  into the sample's vault leaves at her sprint. Letting go of a hang falls from still 15 cm off the wall,
  the hop back with nothing behind pushes off at 250 out and 150 up, the roof-edge guard's leap leaves at
  its 450 cm/s (or her speed before the press), as below.
- Traversal clips (2026-11-10; our own mantle used to stretch the root motion of one 1 m clip over every height
  from 40 to 200 cm). Vault and mantle play the sample's traversal montages (`AM_`), loaded when play
  begins, and their root motion moves the capsule, warped by the engine's Motion Warping to the probe: the
  hands (the clip's `attach` bone) onto `FrontLedge` (the top front edge, facing the wall), the body over
  `BackLedge` (the back edge) and down onto `BackFloor` (the landing). Before the hands reach the ledge the
  clip is warped to its height; after, it is the clip's own motion. Each clip is played from its start to
  its end time at the rate that fits the move's time. The mantle takes its clip by the top's height: the 1 m
  mantle up to 150 cm (`MantleClips`), the 2.5 m climb above. A move that must end on the probe's own point
  follows its path, the clip only posing the body: the climb from a hang (the 2.5 m climb from where its
  hands reach the edge; a hang is always 145 cm under it, so nothing is stretched), a mantle over a thin
  top onto the floor behind it, the roof-edge guard's mantle and leap, and grab, catch, drop to hang,
  corner, leap and hop. A character with no AnimBP, motion warping or clip (the headless tests) follows the
  plain path for every move.
- The jump key (revised 2026-09-28 after "spamming jump doesn't help me get over the mini walls").
  It probes a fan of 7 rays from -35 to +35 degrees around the stick direction (the facing when
  the stick is centred), middle ray first, each up to 180 cm from the capsule, and takes tops from
  40 cm (the sprint trigger still starts at 60) up to 260 cm, choosing vault, mantle, climb-over or
  ledge grab by height and depth exactly as above; an angled press turns her to the wall before
  the move (and before the sample's traversal, so it finds the same wall). Standing still against
  a wall, the press vaults, mantles or grabs it when the height fits, at any speed and at any angle
  in the fan (revised 2026-09-29 after "if you try to jump moving forward with no momentum, it still
  takes a hundred tries to hurdle over"). A vault lands 25 cm past the back edge or, when the capsule
  does not fit there (the neighbour's lower parapet right behind a party wall), up to 75 cm further
  out, and never on something level with the top; heights are compared 3 cm short of their limits
  because a walking capsule floats that far over the floor. The sample refusing (no montage at a
  standstill) hands the same move to our own vault or mantle. When nothing fits and a plain jump
  starts, the late catch re-probes every tick while airborne, rising or falling, for 0.8 s, 100 cm
  ahead, probing from 5 cm above the feet: a top up to 200 cm above the feet with room to stand is
  mantled (one less than 40 cm above them only when it is 40 cm above where she took off, so a
  running jump at a waist-high wall pops onto it instead of sliding down its face; a second press in
  the air keeps the take-off where she left the ground) (a thin one vaulted only
  within the sprint trigger's 150 cm drop), a ledge 150 cm or more above the feet is grabbed (up to
  260 while rising, 330 while falling; a rising grab takes the 0.35 s jump-to-hang, a falling one the
  0.15 s catch). Landing, a hang, a move, a zip or a slide ends the window. A press in the air probes
  the late catch's way, 180 cm ahead. A press during a move
  is held 0.3 s and fires when the move ends (a press into a hang climbs), so mashing chains
  obstacles. Never a mantle onto a top the capsule does not fit on. Every press that does not
  become a move logs why at Log (`jump: no parkour move: too tall (280 cm, up to 260) at +0
  degrees, 66 cm away, on <actor>`, `no obstacle in the 35 degree fan within 180 cm`, `no room to
  stand on the 88 cm top and no vault over it (no floor within 800 cm below the feet beyond it: a roof
  edge)`), and a late catch that found nothing says so when its window closes. A roof-edge parapet
  and a parapet backed by a taller neighbour are refused on purpose.
- Roof-edge guard (2026-09-29, after a 1679 cm fall in Cameron's log: the refusal was right, but the
  plain jump that followed carried her over the parapet and off the roof). When the jump key's fan
  refuses a parapet (a thin top with a back edge, from the jump key's 40 cm up to 160 cm) whose far
  side drops more than 400 cm below her feet (`FarSideDrop`, measured just past its back edge, 800
  when there is no ground within the probe) and she is moving toward it (50 cm/s at it, or the stick
  within 60 degrees of it), the press does not become a plain jump. If the capsule fits on the
  middle of the parapet's top she mantles onto it with our own procedural mantle (never the
  sample's, which picks its own move) and stays there: while she stands on it, walking off is
  allowed only back toward the roof (`bCanWalkOffLedges` is off otherwise), so the stick still held
  at the drop does not walk her off; from there crouch drops to the hang and the grapple works as
  anywhere. A press during that mantle is dropped rather than buffered (mashing would otherwise jump
  her off the top the moment she stood on it). With no room on the top the plain jump goes ahead
  with her speed toward the drop held at zero until she lands, so she lands short of it. A second
  press within 0.3 s of the guarded one means "I know": from the mantle or the held jump, a quick
  hop (0.35 s, arched to clear the top) takes her just past its back edge and lets her go at 450
  cm/s, or her speed before the press if faster. An open roof edge (no parapet), a parapet over a
  drop of 400 cm or less (the jump key's vault), a standstill with the stick centred, a hang (jump
  climbs) and the sprint trigger (which never vaults off a roof) are unchanged. Every step logs at
  Log ("jump: roof-edge guard: a 88 cm parapet with a 800 cm drop beyond, on ...: mantle onto its
  top and stop there", "on the parapet top ...", "a double tap during the mantle, so she leaps
  over"). `roof_edge_guard.png` in the Kate pass shows her on a tenement's parapet after one press.
- Traversable ledges: the sample detects `LevelBlock_Traversable` actors on the
  `GameTraceChannel1` sweep (our Weapon channel) with four `Ledge_1..4` splines whose up
  vector is the ledge's outward normal. The generator places `BP_TraversableBlock`
  (a child with the sample's level-visual lookups off) along every roof edge of 1 m or
  more, hidden, blocking only that channel, 2 cm proud of the facade; 3,723 on the
  district. Verify checks every spline end against the parapet corners within 5 cm.
  The parapet itself (`generate_city.parapet_ring`, since 2026-09-29) is one ring: the footprint
  minus the footprint offset 30 cm inward (mitred), 90 cm tall, so its outer faces are the facades
  and every corner, convex or concave, is closed flush with nothing standing proud of either face.
  Verify checks no building mesh stands past its footprint by more than 1 cm.
  The hang reads the same splines for the line it shimmies along ("Hang" below).
- Ledges and grapple anchors are not saved in the map. `generate_city.py` writes their
  transforms into `DA_EastVillage_CityProps` (`UCityLedgeData`) and one `ACityLedgeSpawner`
  spawns them at load: all anchors and the ledges within 100 m immediately (about 180 ms),
  the rest nearest-first at 4 ms per frame, done about 5 s after load. The grapple grid is
  rebuilt after the spawn. The district map is 2.8 MB (was 72 MB). Street lamps are still
  saved actors.
- Fire escapes (built 2026-09-26): every building 10 to 30 m tall gets one zig-zag
  escape on its longest road-facing edge (487 of 494 qualify; the rest would hit a
  neighbour, a lamp, or another escape). Since 2026-11-11 a building whose escape would leave the
  street no anchor to reach (by the reach model: its street wall is all escape, the far sidewalk is
  out of range, and with the escape gone a facade anchor is reached) gets none: 4 tenements
  (`_reach.escapes_blocking_street`), 483 escapes, 1,992 landings. Landings 240 x 90 cm with 90 cm rails at 330 cm
  and every 330 cm above, the top one at least 180 cm under the roof, ladders alternating
  between them. The outer rail carries a traversable ledge so mantle and vault work onto
  it. 2,010 landings, stored in the props data asset and spawned nearest-first. Black
  iron `M_SteelPainted`.
- Drop to hang: within 60 cm of an edge with more than 150 cm of drop beyond it (lips up
  to 130 cm), crouch while moving toward the edge or jump while facing away transitions
  to the hang on that edge. From a hang, crouch drops. The catch window is 150 to 330 cm
  above the feet so floor-spaced landings chain: hang, drop, catch, repeat. A drop never
  re-catches the ledge it let go of. Walking off an edge, letting go of a hang, or
  cancelling a zip is a controlled drop; landings over 150 cm get the dip.
- Hang (built 2026-09-29, `ParkourHang.cpp`; tests `Hawkeye.Parkour.Hang*` headless on boxes,
  `Hawkeye.Parkour.DistrictHangCorner` standalone on a tenement corner). There is no hang timeout:
  she hangs until a press or the stick moves her. Every rule below logs at Log (`hang: ...`).
- The ledge line: when a hang starts, the straight line of the edge she holds. First the traversable
  ledge splines (`Ledge_1..4` on the spawned city ledges, the fire-escape rail blocks and the sample's
  traversable blocks, found by an overlap on the Traversable channel within 60 cm): one parallel to the
  edge within 25 cm with its ends within 15 cm of her top, joined end to end with collinear neighbours
  whose ends meet within 15 cm (a row of tenements of one height is one line), then followed along the
  wall up to 300 cm past the data where the same edge carries on. With no ledge data it is probed along
  the wall, up to 20 m each way. A point is on the edge when there is a top within 15 cm of hers 5 cm in
  from the face, open air 30 cm out in front of the face (a wall across the ledge ends it), the face
  itself, and nothing standing on the top at the edge (a chimney ends it). Each end remembers why.
- Shimmy: the stick along the wall (at least 0.4 of it along, so within about 66 degrees; camera
  relative, like walking) moves her along the line at up to 120 cm/s (`ShimmySpeed`, times how far the
  stick is pushed), hands on the edge, feet 145 cm below it, the capsule 36 cm out, as in any hang. She
  stops 25 cm (`ShimmyEndMargin`) short of the line's end so both hands stay on it, where the leading
  hand (22 cm ahead) would meet something on the top, and where a sweep of the capsule meets something
  (a fire-escape rail, a ladder, a wall across the ledge). The stop logs why ("the ledge's end: Chimney
  on the top", "FireEscapeLanding_12 in the way"). The climb, the drop and the auto climb all measure
  the top where she is now, not where she caught it.
- Hands (procedural: the Game Animation Sample and the Mixamo folder have no hang or shimmy clip): from
  her first shimmy in a hang the bow-IK post-process graph's hand IK takes both hands through the strike
  channel (`UParkourComponent::GetHangArmPose`, fed by `UpdateArmPoses`), starting from the pose's own
  hand positions so nothing jumps. The hand furthest behind its place steps (the leading one first) over
  0.22 s, lifting 6 cm off the edge and landing ahead by what the body covers in the step, so the hands
  alternate and sit up to about 25 cm staggered; each step leans the body toward it (the hit lean's
  channel, up to 0.7 of its 5 degrees). A strike's pose overrides it; off the hang the IK lets go over
  0.15 s.
- Corners: pushing into the line's end looks for a ledge round the corner. Outside (a face turning away at
  the end, facing her way along) is tried when she is within 59 cm of the line's end (the 44 cm reach plus
  15 cm of slack, `CornerSlack`); the end margin stops her 25 cm short, since the district's parapets are
  flush with both facades at every corner. Inside (a wall across the ledge facing
  back at her) is tried when that wall stopped her within 44 cm. The other ledge's top is within 15 cm of
  hers, its corner end within 60 cm (`CornerReach`) of this one's, both hands fit on it, and the capsule
  fits where she will hang: 25 cm (outside) or 38 cm (inside, clear of her old wall) from its corner end,
  or up to 15 cm further along it if the capsule does not fit right there. Outside, the swing a quarter, half and three
  quarters round must be clear too. Then a 0.4 s move (`HangCorner`): outside, round the point where the
  two faces meet, through the open side (the angle swings the way she turns, the distance from the corner
  blends from start to end); inside, turning where she is. She turns by the corner's angle and the camera
  turns with her. Anything else and she stops at the end (a taller wing, a 30 cm wall end, a ledge
  starting past 60 cm, a neighbour in the swing), and the log lists every candidate's reason.
  `Hawkeye.Parkour.DistrictHangCorner` found 4382 outside roof corners in the district's ledge data (each
  counted from either face), 1558 of them open on both faces with the swing clear, hanging 30 cm from the
  corner (most of the rest meet a neighbour); on the nearest (17.8 m up, 32 m from the start) she
  shimmied 120 cm/s, reached the end margin 25 cm from the corner, turned it in 0.40 s onto the other face
  25 cm round and turned back, never inside geometry.
- Hang jumps. Jump with the stick at least half pushed and at least 0.6 of it to one side leaps sideways
  (`HangLeap`, 0.3 to 0.45 s by distance, a 25 cm arc, the catch clip reaching for the edge) to the first
  ledge past where a shimmy would stop that way: its top within 40 cm of hers, its face from 60 cm behind
  to 150 cm out from hers (a fire-escape landing's rail stands 95 cm out), the new hang 25 cm in from its
  near end and at most 250 cm (`HangLeapReach`) from where she hangs. A ledge farther off, a blocked way
  or no room refuses it: she stays hanging and the log gives the distance ("the nearest ledge to the right
  is 285 cm off"). Jump with the stick at least 0.6 back turns her round (180 degrees, the camera with her)
  and hops (`HangHop`, 0.5 s) to a wall facing her within 200 cm (`HangHopReach`) of the capsule with a top
  from 150 cm below hers to 40 above; with none she turns round and pushes off (250 cm/s out, 150 up), a
  controlled drop with the late catch armed. A plain jump still climbs.
- Auto climb: the stick held toward the wall (within about 32 degrees, at least half pushed) for 0.5 s
  (`AutoClimbHoldSeconds`) climbs, when there is somewhere to stand. Not when the top is a parapet (a back
  edge within 120 cm) with more than 400 cm (`RoofEdgeGuardDrop`) below its top on the far side: there the
  roof-edge guard stays in charge and only the jump press climbs; the stick's refusal is logged once a hold.
  A roof parapet from outside (the roof 90 cm under its top behind it) climbs.
- Hang camera: the control yaw is eased back (at 6/s) to within 65 degrees (`HangCameraMaxYaw`) of looking
  straight at the wall, so the arm never swings behind the wall's plane into it; the lens's shoulder offset
  moves to the open side (the left once the camera's right points into the wall past 0.35, back once under
  0.15) over 0.5 s, and back to the right after the hang. A corner or the hop carries the camera round with
  her. Measured in `Hawkeye.Screenshot.Hang` (the stick injected through IA_Move, a shimmy along a tenement
  parapet and round its corner): over 538 frames the lens stayed 354 to 392 cm from the pivot, never moved
  more than 7.7 cm between frames, and the arm's collision never pulled it in.
- Grapple launch: the zip starts from a launch point 120 cm above the feet, reached by a
  0.15 s hop, and ignores her own roof, parapet, and fire escape until 250 cm plus the
  capsule radius clear of the start (plus the 150 cm allowance at the anchor). Level and
  downward lines are allowed. Jump or crouch mid-zip lets go with the catch active. 62 of
  63 anchored roofs within 150 m of the start have a clear roof-to-roof zip.
- Chaining (revised 2026-09-26): the chain input is allowed from 40% of the line; a chain
  arrow fired mid-zip arrives instantly and the direction of travel turns onto the new
  line over 0.1 s. After a mid-air chain the old anchor's building counts as a start
  support until she is clear of it. The scripted lap shows a true mid-air redirect with
  zero touch-downs inside the chain.
- Zip movement (revised 2026-11-09; the zip was a constant 1800 cm/s placement that stopped her
  dead on arrival and dropped her straight down on a let-go). The zip is a custom movement mode on
  `UHawkeyeMovementComponent` (`MOVE_Custom`, `EHawkeyeCustomMovement::Zip`): the capsule is swept
  every sub-step and `Velocity` is what it really moved over the time. Entry keeps her momentum:
  speed along the line starts at her velocity along it, clamped to 600 cm/s (`ZipMinStartSpeed`) and
  1800 (`ZipSpeed`), taken when the zip starts (from the ground the 0.15 s hop comes first, and
  reports its own velocity), and rises at 6000 cm/s² (`ZipAcceleration`) to 1800. A chain keeps the
  speed; only the direction turns. Arriving, she is on the landing point, Walking (Falling when there
  is no floor under it), moving along the line's horizontal direction at the lower of her speed and
  600 cm/s (`ArrivalCarrySpeed`). Letting go (jump or crouch) falls with the zip's velocity times 1.0
  (`ReleaseVelocityScale`); jump adds 350 cm/s up (`ReleaseJumpBoost`), crouch nothing; either is a
  controlled drop. Blocked, she falls with zero velocity: a wall is not a launch. Speed is integrated
  exactly per sub-step, so a zip covers the same distance in the same time at 30 and 120 Hz.
- Landing above 400 cm while moving is a roll: 0.5 s, 200 cm along the stick, capsule at
  half-height 50, camera pitched down 8 degrees and back, input locked 0.35 s, a single
  forward tumble of the body. Landing while still is a 0.4 s stumble from 0 back to run
  speed. Both procedural; the sample has land clips but no roll. Grapple landings never
  trigger either. Fall damage is still zero below 900 cm. Fall damage begins at 900 cm and never kills from a
  rooftop you can reach by grapple.
- Camera looking up: between +20 and +60 degrees of pitch the arm shortens 350 to 220,
  the socket Z rises 60 to 110, and the pivot lifts 0 to 150 cm in world space (without
  the lift the lens sat on the pavement). Pitch is clamped to [-70, +75]. Above about
  53 degrees Kate drops out of the bottom of the frame rather than filling it.
- Grapple arrow (built 2026-09-26): valid anchors are within 2500 cm, farther than 300 cm
  (so the anchor just landed beside doesn't stay lit), within 30 degrees of the camera
  forward, on the screen 2 degrees inside every edge (`IsOnScreen`, the camera's field of view
  and the viewport's shape; 30 degrees up is off a 16:9 screen), and in line of sight from the
  camera; see "Grapple picker" for which one is marked (a green diamond with the key under it, Q
  or RB, every time; see the next bullet). Fire spawns a straight, gravity-free
  arrow at 6000 cm/s; on arrival the character zips along a straight line to the anchor's
  landing point in the zip movement mode ("Zip movement" below), input locked except the
  camera, and lands with no fall damage. Chaining is allowed once past 40% of the line (see
  "Chaining"), when the marker returns. A zip is cancelled by static geometry on the path except within
  150 cm of the anchor (corner anchors share walls with neighbours). Grapple arrows are
  unlimited (revised 2026-09-29, "grapples should be infinite - it is definitely part of
  traversal"): a shot costs nothing and is never refused for want of one; the arrow stays in
  the anchor until she is within 200 cm of it and is then tidied away (nothing goes back in the
  quiver, and nothing logs a recovery).
- Grapple marker and arrows (revised 2026-09-28 after "there are green diamonds but it no longer
  tells you what button to press"; the key hint used to vanish after the fifth arrow, the diamond
  stayed green with an empty quiver, and anchors whose line was blocked were marked). The target
  also needs a clear zip line from where she is (`IsZipClear`, the same sweep and start and anchor
  allowances as the zip; mid-zip, the line she is on counts as her start), checked nearest the
  middle first, at most 6 zip checks and 32 sight traces per 0.1 s refresh. `GetTargetState()` is what a press would do: Ready
  (green diamond, key hint), ArrowInFlight (grey, no hint), TooEarlyToChain (no marker), None
  (the NoArrows state and its "No grapple arrows" line went with the count, 2026-09-29). A press
  fires only when Ready. An anchor nearer the
  middle than the target that passes everything but the line is drawn as a smaller dim grey
  diamond and is never fired at; `hawkeye.DebugGrapple 1` writes why under it ("the line hits
  FireEscapeLanding_34") and the state next to the key. A zip blocked on the way,
  or left by a chain, reels its arrow back out of the anchor; letting go with jump or crouch leaves it
  in the anchor. Stuck arrows belong to who shot them: the partner never tidies Kate's. Only the
  player's pawn keeps a target (the partner's AI does not refresh one).
- Anchors sit on the parapet centre (15 cm in from the roof edge) at every roof corner
  and at mid-edge on edges over 25 m, on buildings over 8 m, none within 4 m of another.
  Landing points are 60 cm further inboard so the capsule clears the parapet. The
  generator places them; verify asserts every landing point is above a building roof.
  Anchor lookup uses a 25 m spatial grid.
- Grapple picker (revised 2026-09-29 after "many street-side anchors are never targeted" and "the RB
  traversal isn't really working well"; `Hawkeye.Grapple.PickRule.*`): of the anchors that pass range,
  cone, the screen's edges, sight and a clear zip, the marked one is the one nearest the middle of the
  screen; of those within 0.5 degrees of it (`PickTieDegrees`), the one nearest to her. Candidates are
  tried nearest the middle first; a visible anchor behind any number of hidden ones nearer the middle is
  still found (the old 8-trace limit missed it). A green diamond is drawn for the marked anchor and for
  nothing else; since a marked anchor is always on the screen, its diamond is always drawn and a press
  fires at what is drawn. The grey diamond is still the one nearer the middle whose zip line is blocked.
  Level and downward lines are allowed (the launch hop), and the picker has no preference for them: it
  does not weigh height at all. `Hawkeye.Grapple.Audit` checks the picker against a full check of every
  anchor: 0 disagreements in 24,414 dead-on views and 97,656 views 6.3 degrees off (2026-09-29).
- Anchor placement guarantees (2026-09-29, after the campaign lap found the find_arrow roof's one anchor
  hidden from every street spot and the scripted lap never found a third roof): on top of the parapet
  anchors, `generate_city.anchor_spots` bolts facade anchors to the outer face of the parapet (20 cm out
  from the wall, the 40 cm fitting's top flush with the parapet top, landing point 120 cm inboard). Every
  anchored roof gets one on the open wall facing its widest street, placed straight above a sidewalk spot
  where it can be and clear of its fire escape (over the escape only where a spot across the street sees
  past the landings); and one facing each roof across a street or gap (1 to 25 m, nearest points seeing
  each other over no third building as high as the lower roof) whose roof spots reach none of its anchors
  yet. Nothing is removed: the parapet anchors keep their indices. By the generator's reach model
  (`GrappleReach`: the grapple's range, cone, hip camera, sight and clear zip over the building prisms,
  fire escapes and lamps; since 2026-11-11 the zip is Kate's capsule with the game's start and anchor
  supports, and the sidewalk spots are the traversal audit's own), verify_city checks that every anchored
  roof with an open wall on a street and a sidewalk within range has an anchor reachable from the sidewalk,
  and the traversal audit checks the same with the game's traces (it must exit 0), and that the chapter 1 roofs chain:
  reach_roof reachable from the street, cross_block from reach_roof on the roofs, find_arrow from the street
  and from cross_block (the street between them is 24.6 m facade to facade, so the model's chain goes down
  a fire escape and up; the game's own traces in the audit also find a roof route). Geometry the guarantee
  cannot cover is listed, not failed: 21 roofs too tall for 25 m from the pavement or with no sidewalk within
  range, 7 with every wall inside the block or behind a building as tall (all but the 65.5 m tower are
  reached from other roofs). Until 2026-11-11 the audit failed 14 roofs: 5 the model passed, 1 it excused
  (its fire escape filled its 4 m street wall) and 8 it never counted street-facing; see
  `tools-traversal-debugger.md`.
  Across streets and gaps, 88% of the roof pairs in range reach each other by the model, 85% by the audit
  (the rest: a lower roof below her own parapet's line of sight, or a third building in the view); about
  700 more pairs are farther apart than the grapple reaches.

## Bow and arrows (built 2026-09-26; the rules below hold, with these notes)
- Data: `UBowDefinition` (DA_Bow_Kate 0.8 s draw, DA_Bow_Clint 1.0 s) and
  `UArrowDefinition` (DA_Arrow_Standard 40 damage, cap 30, recoverable; DA_Arrow_Grapple
  hit effect Grapple, which makes it unlimited: `UInventoryComponent::IsUnlimitedArrow`; its cap 6
  is unused). `UBowComponent` on the player; `AArrowProjectile`
  with gravity, sticks into what it hits for 30 s, recovered within 150 cm.
- The quiver replaced the weapon slots: `Bow` (null means fists on left click), six
  arrow slots with standard always in slot 1, `ActiveArrowSlot`, keycards. Keys 1 to 6
  and the wheel skip empty slots. The mission grants `StartingBow` and `StartingArrows`;
  CH01 gives Kate's bow, 30 standard, and the grapple (the grant's count, 6, is ignored). `Clear()`
  returns to the grant.
- The grapple slot has no count (2026-09-29): the hotbar, the quiver wheel and the inventory screen
  show an infinity sign where a trick arrow shows "2/4"; it is never spent (`ConsumeArrow` always
  succeeds and keeps it), a grapple pickup, a refill or a reward adds nothing (`AddArrows` only fills
  the slot, returning 0), and it cannot be set to zero. Trick arrows keep their counts and caps.
- Q fires a grapple arrow at the marked anchor without changing the nocked slot, spending
  nothing. Damage scales with draw from 40% to 100% of the arrow's damage.
- Arrows collide as WorldDynamic, not on the Weapon channel, because the invisible
  traversal ledge blocks sit on that channel along every roof edge.
- Reticle while drawing: a ring whose radius is the current spread projected at 1500 cm,
  plus a draw bar that flashes in the perfect window. Drawing forces aim mode and walk
  speed.
- Arrows: 100 cm shaft, 2.5 cm thick, light wood colour with three vanes at 120 degrees
  and a purple nock, so Kate's arrows read as hers when stuck in things. A nocked arrow
  sits on the string while drawing.
- Hands (built 2026-09-26): a post-process animation Blueprint, `ABP_BowIK_Post` (and a
  `_Thug` variant for the old mannequin), built headless by editor-only C++ from
  `create_bow_ik.py`. It links the mesh's own post-process graph, then in component space
  turns the spine 15 degrees side-on plus up to 45 toward the aim (neck counter-turned),
  then two-bone IK on the left hand to the grip and the right hand to the string point,
  alphas blended over 0.15 s. Applied at BeginPlay as a post-process override so the
  motion-matching graph keeps its state. Targets in the head frame (forward, right, up,
  cm): grip (50, -10, -4); string hand from (34, -2, -6) at rest to (-2, 10, -8) at full
  draw. Hands land within 0.8 cm of target. While the bow is up it rides in the IK hand;
  the string nock follows the right palm. `bHolsterWhenIdle` (default on) puts it on the
  back when idle. Fingers do not curl (no finger posing). Kate, Clint, and archers use it.
  A native anim-instance proxy was rejected because a post-process instance can't receive
  the input pose without a Blueprint graph.
- Superseded note: before IK, the bow blended from the left palm socket to a
  point in front of the left shoulder while drawing and rides across the back when
  holstered. Upper-body bow clips now layer over locomotion, with an aim offset on top (see
  "Combat animation clips").

## Trick arrows (built 2026-09-26; effects are custom actors, no GAS; smoke, EMP ring and fireball are placeholder shapes until Niagara systems exist)
- Slot order is fixed: 1 standard, 2 grapple, 3 putty, 4 bola, 5 smoke, 6 EMP, 7
  explosive. Keys 1 to 6, the mouse wheel, and the radial wheel select; explosive is
  reached by wheel or radial. Holding Tab (View on a pad) opens the radial with game time
  at 0.2; release selects the highlighted segment. Tapping Tab still opens the inventory.
- Putty: 10 damage; a thug hit, or within 200 cm of the impact, is staggered then held
  4 s (no movement, AI paused, a blob on him). On a wall it leaves a blob for 10 s. Cap 4.
- Bola: 10 damage; trips a thug for 2.5 s through the ragdoll-and-recover path. Cap 4,
  recoverable.
- Smoke: a 500 cm cloud for 8 s. Thugs inside it, or whose line of sight to the player
  crosses it, are blinded; Kate crouching inside it is undetectable. Cap 3.
- EMP: a 600 cm pulse. Street lamps in the radius go out for 20 s, gunners' pistols jam
  for 6 s. Cap 3.
- Explosive: 80 damage at the centre falling linearly to 0 at 400 cm, knocks thugs down
  2 s, camera shake, a flash. Kate takes the damage too if she is inside. Cap 2.
- Returning arrows (built 2026-11-10 for chapter 3's boomerang; `AReturningArrowProjectile`, rules
  `UHawkeyeReturnFlightRules`, tuning `UArrowDefinition::ReturnFlight`). The projectile moves itself, no drop, at
  `Speed` (2600 cm/s) both ways. Out: straight, sweeping; every body it meets (anything with health, alive, not her or
  her partner) is hurt by the arrow's damage and staggered, then flown through, up to `MaxTargets` (2) in the whole
  flight. It turns back at the first wall or floor, at a switch it trips, after its last target, or at `MaxRange`
  (2200 cm), in that order of reasons (`TurnBack`); a raised shield is a wall. Back: along the path it flew (points every
  50 cm, from where it turned to where it left her), then to her chest where she is now, through anything solid, hurting
  bodies it has not hurt yet while it can (`PointAlongReturn`). Within 70 cm of her it is home: caught when it comes at
  her from within 70 degrees of where she looks (her control rotation) and she is not busy (a takedown, a traversal, a
  zip, staggered, down) (`IsCaught`); else it hits her: a stagger and 2 damage, never more than leaves her 1
  (`SelfHitDamage`), so it cannot kill her. Either way one goes back into her quiver (it is never left lying about). A
  flight over 6 s ends where it is, back in the quiver. Chapter events: `arrow_caught:<asset>`, `arrow_self_hit:<asset>`
  (`Hawkeye.ReturnFlight.Rules`, `Hawkeye.ReturnFlight.OutThroughTwoAndBackInAWorld`).
- The boomerang (`DA_Arrow_Boomerang`, slot 4, the bola's, which Act 1 never fills): 15 damage at full draw, cap 2, an
  orange shaft and yellow vanes; chapter 3 finds it on the grotto's stockroom shelf (2), and chapter 4 carries 2 in.
- Switches (`AArrowSwitch`, a chapter site's `FChapterSiteSwitch`): a box on a wall an arrow trips once. Tripped, its
  red lamp goes out, it makes a noise at itself (`NoiseLoudness`, heard as any noise is, walls and doors counting, the
  instigator Kate), and it reports `switch:<Id>`. With `bReturningArrowsOnly` (the grotto's alarm box) only a returning
  arrow's blunt head trips it; a plain arrow sticks in it and nothing happens.
- Thug AI honours `bHeld`, `bBlinded`, and `bJammed`. CH01 granted 2 putty, 2 bola, 1 smoke and 1 EMP for testing
  until 2026-11-10; Act 1 now unlocks along the campaign order ("Bosses", Campaign order). The putty arrow is the
  story's sticky foam: its display name is "Sticky foam arrow", "Foam" on the hotbar (asset and code keep Putty).

## Bow and arrows, original rules
- Draw is a hold: 0 to `FullDrawSeconds` (0.8 Kate, 1.0 Clint). Release below 25% draw
  cancels. Power scales damage from 40% to 100% and spread from 4 degrees to 0.5.
- Arrows are projectiles with gravity, 6000 cm/s at full draw, penetration off. A hit on
  the `head` or `neck_01` bone multiplies damage by 3 (`UBowDefinition::HeadshotMultiplier`). A perfect release (within 0.1 s of full draw) adds 25% damage.
- Each arrow type is a `UArrowDefinition`: projectile class, on-hit effect, damage, cap,
  and whether it is recoverable. Standard arrows cap at 30 and are always slot 1. Trick
  arrow caps are small (3 to 6). Counts refill at safehouses and from pickups.

## Partner and switching (built 2026-09-26)
- The partner runs a StateTree (`ST_Partner`, built headless by editor-only C++ through
  the StateTree editor API; no GUI needed) with states in priority order: Revive, GoToMark,
  Cover, Attack, Follow. Without a tree (tests) the controller picks the same order in C++.
- Follow: 400 to 800 cm band, settles at 600, sprints beyond 1500. If the height gap is
  over 300 cm and he has been out of range for 5 s, he zips to the anchor nearest the
  player, or teleports to the landing point if the line is blocked; both logged.
- Attack: the enemy the player hit most recently within 3 s (nearest on a tie), with his
  own bow (1.0 s draw, released just after full) at range and bow strikes under 200 cm.
- Cover: a ring of candidate points, keeping those where the shooter's trace is blocked.
- Revive: at 0 health the player is downed, not restarted; after 3 s beside her she is
  back at 30%. Once per fight; a fight ends 10 s after the last contact. He has 4.5 s to
  reach her before he is put beside her (was 12), so the revive lands inside the 8 s down
  (see "Health and damage", downed). (Changed 2026-09-28.)
- The partner can't drop below 1 health, self-heals under 50%, can be staggered. T marks
  a point for him to go to; on a pad it is R3 (press the right stick, which nothing else
  used; added 2026-09-29). Either way the mark is where the reticle sits: a trace straight
  down the camera's centre (the player camera manager's view point), up to
  `MarkTraceDistance`, projected onto the navmesh. The HUD's partner line ends with the
  hint, "Clint: following  [T] send" or "[R3] send" by the last input.
- `SwitchCharacter()` on X or LB: only when the chapter's `bAllowSwitching` is set (CH01 Night
  Shift false: Act 1 has no Clint on screen, and `bPartnerPresent` off benches him; see "Chases"); refused if either character is mid-traversal, mid-zip, mid-takedown, or down.
  Swaps possession, hands the old pawn to the partner controller, rebinds the HUD
  (quiver and name), blends the camera over 0.3 s.
- Banter: `DT_Dialogue`, 48 lines, six per speaker per situation (idle roam, after a
  fight, objective near, low health). One every 45 to 90 s of roaming or on the event,
  speakers alternating, 4 s subtitle, no audio. Clint's arrows are picked up into the
  player's quiver for now. BP_Clint in the map adds PoseSearch index-build warnings at load.

## Lucky (built 2026-11-11; rules in Source/Hawkeye/World/LuckyRules.h, tested in Hawkeye.Lucky.*)
- `ALuckyCharacter`, placed by `generate_city.py` as `City_Lucky` beside the PlayerStart. No gameplay: his capsule
  blocks only the world (Kate, thugs, arrows, the camera and sight lines pass through him), he affects no navmesh, no
  thug perceives him, nothing saves him. No story and no lines yet.
- States: Idle, Walk, Trot, Sit, chosen every frame from the ground distance to the player's pawn, the height gap and
  how long she has stood still (ground speed under 15 cm/s). Numbers are `ALuckyCharacter::Follow`:
  - Out of reach (over 1200 cm along the ground, or over 250 cm above or below him): he stops where he is; sitting, he
    stays sat.
  - Standing or sitting, she is over 350 cm away: he sets off. Moving, he stops inside 200 cm (the gap is the
    hysteresis). Over 500 cm he trots, under it he walks.
  - Near her and stopped: he sits once she has stood still 3 s, and stands again as soon as she moves.
- He walks the navmesh to her (an AIController's MoveToActor, re-asked every 0.5 s, partial paths allowed). His top
  speeds are each clip's own foot speed times its top play rate (walk 30 cm/s x 1.3, trot 92 cm/s x 1.6, about 39 and
  147 cm/s), and the clip plays at his ground speed over its own (0.35 to the top rate), so the feet do not slide.
  She outruns him at a jog: he follows a short way, not across the district.
- No mesh on disk (a fresh clone): he logs one Display line and is hidden, ticking nothing.

## Mission
- A mission has 1 or more objectives. At least one must be non-optional.
- `StartMission` on a subsystem that already has an active mission ends the old one
  without firing `OnMissionComplete`, then starts the new one. Logs a warning.
- `StartMission` fires `OnMissionStarted(Definition)` once and nothing else. It does not
  fire `OnObjectiveUpdated` for the initial objectives; the HUD reads
  `GetActiveObjectives()` and `GetCurrentObjective()` on `OnMissionStarted`.
- `StartMission` with a definition that has no non-optional objectives logs an error and
  does nothing.
- `CompleteObjective(Id)` on an unknown id logs a warning and does nothing. On an
  already-completed objective it does nothing and fires no delegate.
- Completing an objective fires `OnObjectiveUpdated` exactly once for that objective.
- The mission is complete when every objective with `bOptional == false` is complete.
  Optional objectives completed after that point still fire `OnObjectiveUpdated` but
  never fire `OnMissionComplete` again.
- `OnMissionComplete` fires at most once per `StartMission`.
- If `FlashbackToPlay` is set, `OnFlashbackRequested` fires immediately after
  `OnMissionComplete`, in the same frame, with the loaded definition. If it is null,
  the controller proceeds straight to `NextLevel`.
- The current objective is the first incomplete non-optional objective in array order.
  HUD shows that one.
- Unmarked (2026-11-10, chapter 2's courier). An optional objective with `bUnmarked` is never on the HUD: never the
  current objective (no marker, no compass bearing, no title), and its completion shows no "Objective complete" toast
  (`UHawkeyeObjectiveWidget::ShouldToastCompletion`); it still fires `OnObjectiveUpdated` (the autosave hears it) and is
  saved with the rest. On a required objective the flag is ignored. Completing it before, after or never changes when
  the chapter completes (`Hawkeye.Mission.UnmarkedOptionalNeverBlocksOrShows`).
- Campaign flags (2026-11-10). An objective's `CampaignFlag` goes up in `UHawkeyeCampaignState::CampaignFlags` when it
  completes, in the game mode's objective handler before that objective's autosave, once. Flags outlive the chapter
  (the completed objectives do not); only a new game clears them (`Hawkeye.Campaign.FlagRaisedOnceAndSaved`).
  Chapter 2's courier raises `saw_courier` for later acts; chapter 3's ornament `found_ornament`.
- Stealth objectives (2026-11-10, chapter 3's `enter_grotto`). An objective with a `SpottedTitle` is a stealth one: the
  first time a living thug is Alerted while it is the current objective, it is spotted (the chapter site's watch,
  `UHawkeyeChapterSiteRules::ShouldMarkSpotted`, five times a second): its title and hint become `SpottedTitle` and
  `SpottedHint` on the HUD, it fires `OnObjectiveUpdated` (not completed, so no autosave and no toast), and the chapter
  event `spotted:<id>` fires (her line). Nothing fails: it still completes the way it would have. Spotted objectives are
  saved (`UHawkeyeCampaignState::SpottedObjectives`) and a load puts the title back
  (`Hawkeye.Mission.StealthObjectiveSpottedAndTriggers`).

## Chapter events (built 2026-11-10 for Act 1's "Reindeer Games")
- `UMissionDefinition::Events`: entries of an event name, DT_Dialogue lines and arrow grants. `UChapterEventSubsystem`
  fires every entry for an event the first time it happens in a run of the chapter (`UHawkeyeChapterEventRules::Match`):
  the lines through `PlayLine` (queued one after another, fight or not, unlike sequences), the arrows into the
  player's quiver up to each cap. Events: `parry` (the player's parry landed, `AHawkeyeCharacter::TryParry`),
  `arrow_loosed:<arrow asset>` (the player's bow loosed it), `objective_completed:<id>`, `objective_started:<id>` (it
  became the current one; the first objective's at the chapter's start, never on a load), and since 2026-11-10
  `takedown` (the player's stealth takedown), `trigger:<name>` (a site trigger), `switch:<id>` (a switch tripped),
  `spotted:<objective>` (a stealth objective seen), `arrow_caught:<arrow asset>` and `arrow_self_hit:<arrow asset>`
  (a returning arrow home). No chapter is named in C++.
- Reactive lines (2026-11-10, from chapter 2's first-parry line landing after the fight). An entry with `bReactive` queues
  its lines as reactive (`UDialogueQueue::PlayLine(Row, true)`): ahead of every story line already queued, behind any
  reactive line already waiting, and dropped unspoken when it has waited more than `ReactiveMaxLateSeconds` (3 s)
  without starting (the line speaking still finishes). Story lines are never dropped however late. Chapter 2's parry and
  first foam arrow, chapter 3's takedown, boomerang self-hit and spotted lines are reactive
  (`Hawkeye.Dialogue.ReactiveLinesJumpOrDrop`).
- What has fired is mirrored into the save (`FiredChapterEvents`) and put back on a load, so a load mid-chapter says no
  line and grants nothing twice; a chapter started afresh (chapter select, a new game) starts with nothing fired.
  `Hawkeye.ChapterEvents.FireOnceARun`.
- Chapter 2's: arriving at the drive speaks her two lines, the crew's bark and her reply, then the foam unlock line and
  grants 3 foam; the first parry, the first foam arrow ("Stay."), the crew down, the toys back (the volunteer) and the
  courier each speak theirs. The script has the foam unlocked after the first parry; it unlocks on arrival so a player
  who never parries still gets it.

## Chapter sites (built 2026-11-10 for Act 1's "Reindeer Games")
- A chapter's set piece on the district is data: `UChapterSiteDefinition` (`UMissionDefinition::Site`) of props (static
  meshes, blocking or not), point lights, objective volumes (`AObjectiveTriggerVolume`), a crew (thugs with a class, weapon,
  place and facing), the crew's objective (an `AThugGroupObjective` at their middle: every one down completes it) and the
  objective that wakes them, keycard pickups, and examines (`AChapterEndInteractable`: objective, prompt, prop, whether it
  must be the current objective, the keycards it asks for, its close-up). `UChapterSiteSubsystem` spawns it when the
  chapter starts and clears every actor when another starts; nothing is placed in the map.
- The crew stand still (thinking off) until the alert objective completes (none: at once), then are alerted on her. They
  are never saved by SPUD (`bSkipSave`): on a load the site is spawned again from the mission start and matched to the
  save (`Reconcile`): the crew gone if their objective is done, awake if the alert one is; a pickup whose keycard she
  carries is not on the ground.
- The chapter's opening puts Kate at the site's `PlayerStart` (zero leaves her where she is).
- An examine can ask for keycards (`AChapterEndInteractable::RequiredKeycards`): offered only when she carries all of
  them. Kate's keycard ring is saved with her (`SavedKeycards`), so a load keeps what she carries.
- Since 2026-11-10 (chapter 3) a site also has: `Map` (the map it stands in; none is the district, any map that is not
  an interior), interior doors (an `AInteriorEntrance` with its interior, its doorstep's return point and a tag an
  objective's marker can follow), return points (a target point tagged with its label: where an interior's front or roof
  door comes back out), triggers (a box that reports its chapter event the first time Kate stands in it; `bOnlyUnseen`
  waits while any thug is Alerted: an overheard conversation, `UHawkeyeChapterSiteRules::ShouldFireTrigger`), switches
  (`AArrowSwitch`, above), and examines that wait for an objective (`RequiredObjective`: the ornament after the roof). The
  site's watch (a 0.2 s timer, `UChapterSiteSubsystem::Watch`) fires triggers and spots stealth objectives; it runs
  with no site too.
- `Hawkeye.ChapterSite.ChapterTwoCompletesInAWorld`: a site built like chapter 2's in a test world: spawned by the
  mission start, the crew still until the arrival, the courier offered out of order, the table refused without both
  sacks, the chapter complete without the courier, a load matched, another chapter clearing it.
- Objective ids are `FName`, unique within a mission, lowercase snake_case
  (`find_weapon`, `reach_stairwell`). Reusing an id across missions is fine.

## Chapter interiors (built 2026-11-10 for Act 1's "The Grotto")
- A chapter carries on into an interior one of its sites names (`UMissionDefinition::InteriorSites`, each with its
  `Map`; `UHawkeyeChapterMaps::ContinuesInto`). Content picks its map by `UHawkeyeChapterMaps::BelongsInWorld`: a site or a
  boss naming a map belongs only in that map, one naming none only in maps that are not interiors (the interior game
  mode's indoor camera), so the district's site never stands inside the grotto and the grotto's Santa never on a roof.
  Another interior (the auction house) during a chapter is as before: no chapter inside, saves refused.
- In by its door: the district is saved (the entry save) and the campaign captured; the interior starts the chapter
  where the campaign has it, as a load does (no title, no opening, no objective_started; `bRestorePending`), and
  announces it (`UHawkeyeSaveSubsystem::FinishContinuation`: `OnCampaignLoaded`, so the interior's site, the chapter's
  fired events and the boss's checkpoint match the campaign). Her quiver comes with her as through any interior door.
- Inside, saves are allowed (`FHawkeyeSceneReturn::AllowsSave`: never on the way back, never in a flashback's scene or
  a plain interior): objective autosaves, the 60 s roaming autosave, quit to menu. The save is of the interior and keeps
  where its doors lead (`UHawkeyeCampaignState::InteriorReturnMap`, `InteriorReturnPoint`); a load (Continue, a death's
  reload) comes back inside with its exits working. Once the slot holds the interior, the entry save is gone.
- Out by a door: the campaign is captured inside and carried out. With the entry save still in the slot it loads (the
  district as she left it) and the carried campaign replaces the one it held (everything that happened inside); with
  the slot holding the interior the district opens fresh and the chapter carries on into it from the campaign the same
  way. She is put at the door's return point (the front door: the doorstep; the roof stairs: the roof point).
  `Hawkeye.ChapterMaps.ContentBelongsInItsMap`, and played by `Hawkeye.Lap.Grotto` (in, a save and load inside, out by
  the roof).
- Doors an objective opens: a layout door with `"opens_on"` (`ADoorActor::OpensOnObjective`) unlocks and opens by itself
  when that objective completes, or at once when it already has (a load past it); while it waits it answers no
  interaction (the keypad beside it opens it) (`Hawkeye.Door.OpensOnObjective`).
- A layout enemy may name his Blueprint (`"blueprint"`: the grotto's `BP_Thug_Elf`); his type still picks his weapon.

## Objective markers and compass (specified 2026-09-26)
- An objective trigger volume registers its centre with the tracker for its id at
  BeginPlay and unregisters at EndPlay; a thug group objective registers its location the
  same way; a data asset never needs coordinates. Priority: an explicit `WorldLocation`,
  then the actor named by `MarkerActorLabel`, then the registered point.
  `GetCurrentObjectiveLocation` returns the current objective's point if it has one.
  (Built 2026-09-26; 181 tests.)
- The HUD shows a cream diamond marker projected onto the current objective with the
  distance in metres under it, clamped to the screen edge with an arrow when off-screen,
  hidden within 3 m. A 400 px compass strip at the top centre shows ticks every 15
  degrees, N/E/S/W, and the objective's bearing.
- Completing an objective shows a 2 s "Objective complete" toast; the next objective
  shows a 2 s "New objective" toast. While the chapter title card is up (fade out included) every
  toast is held: queued, not drawn, its clock and sound waiting; when the card has gone the front one
  comes up for its full 2 s with its sound (`Hawkeye.Objective.ToastsWaitForTheTitle`; the campaign
  lap checks it under the card and after it). Hints wait out the card too (below).
- The retired Rooftops chapter's objectives each have a 1 m beacon with a purple emissive top and a 300 lm
  purple point light; tagged `objective:<id>`, they stay dark while Night Shift runs.
- An objective may carry a `Hint`, a second line under its title in the HUD. (2026-09-28) Night Shift's
  have none.
- The compass also shows every safehouse (a filled house once found, a hollow one before)
  and every challenge pedestal within 150 m (a purple medal). A pedestal within 80 m gets
  a world marker (medal and distance) when the current objective is farther, not while a
  challenge runs. A marked safehouse's marker is a house with its distance and name, a "?"
  in it and "[Unknown safehouse]" until found; a marked pedestal's is a medal. (2026-09-28)

## Flashback
- `Play(Definition)` with a null definition or zero slides finishes immediately and
  fires `OnFlashbackFinished` once. It never leaves the game paused.
- Each slide occupies `HoldSeconds + CrossfadeSeconds`: it displays fully for
  `HoldSeconds`, then crossfades for `CrossfadeSeconds` into the next. The last slide
  crossfades to black instead. Total duration is the sum over all slides of
  `HoldSeconds + CrossfadeSeconds`, and `OnFlashbackFinished` fires when that elapses.
- Skip (any key, if `bSkippable`) finishes the whole flashback, not the current slide.
  Skip during the first 0.5 seconds is ignored so a held key from gameplay can't skip it.
- Playback pauses the game (`SetGamePaused(true)`) and restores the previous pause state
  and input mode on finish. If the world is torn down mid-playback, no crash and no
  dangling pause.
- Voice lines play at slide start. A voice line longer than `HoldSeconds` is cut off at
  the crossfade; the data should be tuned so this doesn't happen.

## Health and damage
- `CurrentHealth` is clamped to `[0, MaxHealth]`.
- `ApplyDamage(Amount <= 0)` does nothing and fires nothing.
- While `bInvulnerable`, damage is ignored entirely: no `OnHealthChanged`, no `OnDeath`.
- `OnHealthChanged(Component, NewHealth, Delta, Instigator)` fires on every change, including heals. The
  dead flag is set before `OnHealthChanged` broadcasts on the killing blow, so a listener
  that checks `IsAlive()` inside that callback already sees false.
- `OnDeath` fires exactly once when health first reaches 0, after `OnHealthChanged`. Further damage after death
  does nothing. `Heal` after death does nothing; revive is a separate explicit call
  (`Revive(NewHealth)`) that resets the dead flag.
- There are no damage type assets. Melee goes through `ApplyMeleeDamage`, which also fires
  `OnStaggered` when the damage landed, the strike allows a stagger and the amount is above
  `StaggerThreshold` (10). Plain `ApplyDamage` (a bullet) never staggers; an arrow's hit calls
  `Stagger` itself where it should. The explosive's blast falls off linearly from its full
  damage at the centre to 0 at its radius for everyone inside, and hurts the shooter's partner
  at half (see "Trick arrows").
- `IncomingDamageScale` (1 for everyone) multiplies every hit before it lands: a boss's guard (0.35) and a scripted
  beat (0, so nothing lands and nothing fires) set it; nothing else touches it.
- `bCannotDie` keeps a component at 1 health or more: damage still lands and still staggers,
  but `OnDeath` cannot fire. The partner carries it.
- Downed (built 2026-09-28, playtest note 9): at 0 health the player is down, never stuck.
  The down lasts `DownedMaxSeconds` (8) at most. With the partner's revive coming the HUD
  shows a ring emptying over the 8 s and "[Clint is coming]" (his name), "[Hold on]" once
  he is within 250 cm; with none coming (spent this fight, or no partner) the ring and a
  "[Any key]" placeholder, and any key but the pause keys ends it, ignored in the first
  0.5 s so a held attack cannot skip it. No revive by 8 s, or the key: she dies of it,
  "[You're down]" over a 1.5 s fade, and the last autosave loads (last checkpoint or
  safehouse). A revive after that does nothing.
- Nobody attacks a downed player: a thug whose target is down drops a burst or draw,
  lets a swing already under way finish (it cannot hurt her: the health is at 0 and
  dead-flagged, so no damage lands at all), then circles her at 450 cm. Back up, they come
  again.
- The reload resets the fight: placed thugs come back from the save (alive at full health
  and calm, or dead), crime thugs are not saved so they are gone, a challenge run is over,
  the input lock and the grey post process go with the old pawn. Dying (or Restart) in a
  flashback's playable scene now ends the scene too, as leaving it would (before, every
  later save was refused as "a playable scene is running").
- Player health regenerates after `RegenDelay` seconds without damage at
  `RegenPerSecond`, up to `MaxHealth`. Guards and bosses never regenerate.
- Headshots: `UBowDefinition` multiplies an arrow's damage by `HeadshotMultiplier` (3) when the
  hit bone is in `HeadBoneNames` (`head`, `neck_01`); the thugs' pistol does the same with its own
  set. A thug has 100 health on Normal. A full-draw standard arrow does 40, so he dies to one
  headshot (120) or three body hits; two perfect releases (50 each) kill too.

## Boss phases
`UBossPhaseComponent` is the phase rule under every boss ("Bosses", below, is the fight around it).
- Phases are ordered by descending `HealthThresholdPercent`, a fraction of max health (1.0 is full); `Bind` stable-sorts
  them, so phases with the same threshold keep their authored order. Phase 0 is active from the start (threshold 1.0)
  and entering it is silent: no `OnPhaseChanged`. A boss with N phases has exactly N-1 transitions.
- Each later phase has an `Entry`. `HealthBelow` (the default): it opens when health percent drops strictly below its
  threshold. `CrewDown`: it opens when `NotifyCrewDown` says the phase before it has no crew left standing; a crew
  flag opens only the phase right after the current one. The walk (`UBossRules::ResolvePhase`) goes on through every
  next phase whose entry is met and stops at the first that is not, so overkill that crosses two thresholds advances
  to the lowest matching phase and fires `OnPhaseChanged` once, with `NewPhase` the final one, and no amount of damage
  skips a phase a crew opens.
- The killing blow never advances a phase, even if it also crosses a threshold. Health reaching 0 fires `OnDeath` from
  the health component; the phase component stays on whatever phase it was in.
- `bBossInvulnerable` holds the boss out of a whole phase (he watches his crew): set when the phase is entered,
  dropped when a later phase is. It is not a scripted invulnerability: a transition out of such a phase does not
  restore it.
- During a transition with `bInvulnerableDuringTransition`, invulnerability is raised first, then `OnPhaseChanged`
  broadcasts (so listeners see the boss already protected), then after `TransitionSeconds` it goes back to its
  scripted state (a boss already invulnerable for scripted reasons stays that way) plus the new phase's own. With no
  world to run a timer, or a 0 s transition, that happens straight after the broadcast.
- `RestorePhase(i)` (a checkpoint) makes phase i current with its own invulnerability, no broadcast and no transition.
- `BehaviorTag` names the phase's behaviour for the log and for a boss with a tree of its own; chapter 4's boss is
  driven by his definition ("Bosses").

## Bosses (built 2026-11-10 for Act 1's "The Social Club"; rules also in Source/Hawkeye/Boss/BossRules.h)
- A boss is data: a `UBossDefinition` (`DA_Boss_<chapter>_<name>`, written by `Tools/Editor/create_boss.py`): his
  name, body class (a thug Blueprint with his look), max health, body scale, move set, his phases (each an
  `FBossPhase` rule plus its objective, whether he fights or watches, its crew, whether the arena's hazard is loose,
  its start and end lines, its barks and the arrows it grants), the arena (the roof's outline and floor, the fight's
  centre and radius, the street stand and the parapet anchor it grapples, his spot, the hazard's lane, mesh and warning
  strip, the props and lights) and the hazard's tuning, the meet objective, the pre-fight and after sequences, and the
  map the fight is in (`Map`; none is the district's roofs; chapter 3's Santa names L_Int_Grotto, his "roof" the
  workshop's floor, no street stand and no hazard). A
  chapter names it on `UMissionDefinition::Boss`; `UBossFightSubsystem` runs it. A second boss is a second asset.
- When the chapter starts (or a save of it loads) the roof is dressed, the boss stands at his spot and the first
  phase's crew round the fight's centre, all calm and not thinking, the boss turning to face Kate, every one of them
  `bSkipSave` (SPUD never brings them back; the checkpoint does). The objective marker is the fight's centre. The
  chapter's opening (not a load) puts Kate on the arena's street stand, facing the roof, in grapple range of its
  anchor.
- Meet: her feet inside the roof's outline, no more than 60 cm under its floor or 250 over it, complete the meet
  objective. The pre-fight sequence plays; the fight starts when it has finished (after at least 1 s), at
  `IntroMaxSeconds` (45 s), or the moment she hurts one of the crew.
- A phase starting: its crew (the first phase's are already there) think and are alerted on her; the boss fights
  (thinking, alerted, his move set) or watches (still, facing her) as the phase says; the hazard comes loose if the
  phase says; its arrows go into her quiver (up to their cap); its start lines speak.
- A phase ending: its end lines speak and its objective completes, then the next phase starts (the objective's
  autosave is therefore taken before the next phase grants anything). A crew phase ends when every one of its crew is
  down (limp or dead). Every phase a big hit skipped ends the same way, in order.
- The boss: `bRefusesTakedown` (no stealth kill on a boss), max health `MaxHealth` times the difficulty's thug health
  scale, never regenerating, melee damage above `StaggerThreshold` staggers him (40: Kate's light strike, 15, does
  not, so he swings through it; her heavy, 35, does not either), her heavy does not knock him down
  (`bResistsKnockdown`: he leans back from it; a blast or a bola still puts him over), his swings are the move set's
  `Combo` in order through the thug's own tree (`ST_Thug`'s melee, attack tokens and all: he asks for a token like
  anyone), and he does not back off after two swings.
- His finisher (second pass, 2026-11-10): with the move set's `bFinisherOnlyWhenWinded` (on by default) a finisher
  takes him only inside his winded window (`AThugCharacter::bFinisherNeedsGrantedWindow`; the rule is
  `UBossRules::IsFinisherOpen`). A parry, a combo ender or a knockdown opens nothing on a boss; on a thug each still
  does. A parry staggers him at most `ParryStaggerSeconds` (0.6; a thug takes her full 1.5). The punish is the
  window, not the parry.
- Unblockable swings: an attack with `FHawkeyeMeleeAttack::bUnblockable` cannot be parried (`ClassifyParry` says
  None, so no parry line and no parry hint either). Its telegraph is its own: "!!" instead of "!", in a hot yellow
  (1, 0.85, 0.05) kept apart from every palette's danger colour, flashing to white six times a second (reduce
  flashing dims the flash), with the dodge key over it as a hint draws a key ("[Ctrl]", "[B]"). A dodge started
  through it counts as a perfect dodge, as a parryable swing's does. Every thug's swing leaves it off.
- Winded: once `WindedAfterSwings` swings have started since the last winded window (or the phase start) and the
  last has finished, he is winded for `WindedSeconds`: held where he stands (a stagger he cannot leave), no swing,
  the finisher open, every hit on him doing `WindedDamageScale` times its damage. A finisher on him takes
  `FinisherDamageFraction` of his max health (killing him only when that is all he has left), and closes the window:
  one finisher a window. A broken guard or the hazard catching him winds him at once the same way
  (`FBossComboClock::ForceWinded`: counted, never stacked on a window already running).
- Guard: a phase's `GuardDamageFraction` under 1 gives him a guard while he fights and is not winded: every hit on him
  lands at that fraction (`UHealthComponent::IncomingDamageScale`, which the fight sets every tick by
  `UBossRules::BossDamageScale`), so chipping at him is slow but never nothing. Winded, the guard drops and hits land
  at `WindedDamageScale`.
- Guard breaks: an arrow whose effect is one of the phase's `GuardBreakEffects`, landing within `GuardBreakRadius`
  (300 cm) of him (or on him) while he guards, breaks it: he is winded at once (`UBossRules::BreaksGuard`). The fight
  sees each trick arrow's effect actor once. There is no zip-kick or grapple strike in the game, so arrows are the
  only guard-break source the data can name today.
- The hazard and him: in a phase with `bHazardWindsBoss`, the sweep catches him by the same lane and height rule as
  her (`ABossArenaHazard::Reaches`, his own `HitCooldownSeconds`): no damage, a shove out of the lane, and he is
  winded. That is the lure: stand across its lane from him while it telegraphs. Props with `bHazardPushes` (the
  presents) are shoved along the lane ahead of its body when its sweep reaches them, never passed through
  (`UBossRules::PushedAlong`: from half a body behind to a body's reach ahead, never more than a reach past the lane's
  ends, so the sweep back leaves a prop where the last one put it). A putty arrow landing within `PinRadius` (250 cm)
  of its body or its tether pins it for `PinSeconds` (4 s): its clock stands still, no sweep, no hit, the lane dark,
  then it runs on from where it was (`UBossRules::PinsHazard`).
- Beats: a phase's `TransitionBeat`, and the definition's `DefeatBeat` on the blow that puts him down, when their
  `Seconds` are over 0: her input held (`AHawkeyeCharacter::SetScriptedLock`, which `IsLockedOutByTakedown` includes,
  and the controller's move and look input ignored), nothing lands on her or him (both health components' incoming
  scale 0), the boss and the crew stop thinking and turn to her, the hazard held, the view cut over `BlendSeconds`
  (0.3) to a camera at `CameraOffset` in the frame of the flat line from her to him (X along it from their middle, Y
  to its right, Z up), looking `LookAtBoss` of the way from her to him at chest height and pulled in front of
  anything solid like a spring arm (`UBossRules::BeatCamera`), the beat's lines spoken. Over at its `Seconds`, or on
  Space / pad A after its first 0.4 s (`UBossRules::IsBeatOver`); then the view blends back, everyone fights as the
  phase says, and a defeat beat hands over to the after sequence. A beat that comes from her finisher's blow waits
  for the finisher to finish.
- The finisher that ends something: a finisher on him whose blow (winded scale included) kills him or puts him under
  the next phase's health threshold (`UBossRules::IsDecisiveFinisher`) runs with the definition's
  `FinisherCamera` instead of her own numbers: time 0.25 for 0.8 real seconds and the camera 110 cm in (a thug's:
  0.5, 0.4 s, 40 cm). Her numbers come back when it ends.
- His look: a light rides on him (`RimLightColor`, `RimLightLumens`, `RimLightRadius`), 110 cm in front of him and
  over his shoulder, so a dark suit reads at night.
- Lines during the fight are DT_Dialogue rows spoken with `PlayLine` (they do not wait out a fight as sequences do),
  queued one after another. Barks are a phase's lines for a moment: his swing landing, her dodge within 450 cm of his
  swing, winded, her finisher on him, a parry of anyone, one of the crew going down, a bat swing from the crew
  starting, the first of the phase's granted arrows loosed. Each speaks at most once a fight unless the data says it
  repeats (then not within its cooldown).
- The hazard (`ABossArenaHazard`): tethered at its lane's start (swaying) until a phase looses it. Then, on
  `UBossRules::HazardStateAt`'s clock: `FirstDelaySeconds` waiting, then over and over a telegraph (the lane's strip
  lit on the roof, a red light on it, a shudder) for `TelegraphSeconds`, a sweep along the lane for `SweepSeconds`, a
  rest at the far end for `RestSeconds`, each sweep going back the way the last came. It hits Kate only while it
  sweeps, only when her feet are within `HalfWidth` across the lane and `HalfLength` along it of its body (and between
  150 cm under it and 350 over), at most once per `HitCooldownSeconds`, for `Damage` times the difficulty's thug damage
  scale, and shoves her out of the lane the short way. It never blocks anything; besides her it reaches only the boss
  (in a phase that says so) and the props it pushes (above). It settles where it is when the boss goes down. The
  telegraph's light and strip are pure red (1, 0.02, 0.01 and the strip's emissive at 0.25), since anything with green
  in it bloomed orange on the snow.
- Down: when the boss's health reaches 0 the hazard settles, the phase's end lines speak, then the after sequence; the
  remaining phase objectives (the last completes the chapter) complete when the lines are over (after at least 1 s)
  or at `AfterMaxSeconds` (60 s).
- Save: the fight is not saved mid-phase. A load (Continue, a death's reload) restarts it at the checkpoint the save's
  completed objectives give (`UBossRules::CheckpointPhase`): before the meet objective, the approach (all on the roof,
  waiting); else the first phase whose objective is not complete, with the boss at that phase's checkpoint health (full
  for the first phase and for one a crew opens, on the threshold for one health opens: `CheckpointHealthFraction`),
  the phase's crew fresh, the hazard loose if the phase looses it, fighting at once, and its arrows granted again (the
  save was taken before the phase granted them). The checkpoint is the transition beat's end state: no beat plays,
  nothing is held. No line is said twice in a run of the fight: story lines (a phase's start and end lines, a beat's)
  go through `UBossRules::UnheardLines`, and what was heard ("line:<row>", "bark:<phase>:<event>") is kept in the
  campaign (`UHawkeyeCampaignState::BossHeard`, written with every save) and in memory across a death's reload
  (`UHawkeyeSaveSubsystem::SessionBossHeard`, since the reload reads back a slot older than the lines said since);
  a load reads both back, so a once-a-fight bark heard before it stays heard. The chapter's opening and a new game
  clear them. Kate, her health and quiver come back from SPUD as always. Restart from a phase checkpoint was chosen over a mid-phase save because the boss's swing,
  winded and hazard clocks, the crew's health and the dialogue queue would each need saving and restoring, and a
  phase is short.
- Street crimes wait while a fight is past its approach.
- Campaign order: the chain of `UMissionDefinition::NextChapter` from the district's `StartingMission` (Night Shift,
  Reindeer Games, The Grotto, The Social Club). Act 1's arrows unlock along it, and a chapter's `StartingArrows` hold
  everything unlocked before it, so chapter select can open any chapter cold: chapter 1 the standard arrow and the
  grapple; chapter 2 the same, and the sticky foam (the putty arrow, shown as "Foam") granted by its arrival event;
  chapter 3 the standard, the grapple and 2 foam, and the boomerang (2) found in the grotto; chapter 4 the standard, the
  grapple, 2 foam and 2 boomerangs, and the confetti in its last phase. Chapter select in the safehouse lists it: a chapter at or
  before the one in progress is open, the one after it opens once it is complete, the rest say "[Locked]"; picking an
  open chapter that is not the one in progress starts it (`AHawkeyeGameMode::StartChapter`: the chapter before is put
  away without the replaced-mission warning, then the mission, its opening, and a save 1.5 s later). `-HawkeyeChapter=<mission asset>` on the command line starts the district on that chapter,
  and the console command `hawkeye.StartChapter <mission asset>` starts it at once (laps and tests).

### Chapter 4's boss, Tomas (`DA_Boss_CH04_Tomas`; numbers on Normal; second pass 2026-11-10)
- 820 health (656 / 820 / 984 by the difficulty's thug health scale; 680 until 2026-11-11, when blows landing on contact took the scripted fight down to 80 to 105 s, under its 90 s floor; 400 before the second pass), drawn and sized 1.12
  times a thug, `BP_Boss_Tomas`: BP_Thug in a green tracksuit with gold stripes and no ski mask, a cool light (0.75,
  0.85, 1.0; 260 lm over 380 cm) riding in front of him so he reads at night.
- Combo: a jab (12 damage, 0.75 s wind-up, 0.5 s recovery, 150 cm; parryable), a jab (12, 0.6 s, 0.5 s; parryable), a
  haymaker (22, 1.1 s wind-up, 0.8 s recovery, 170 cm, shoves 200 cm; unblockable: dodge it); 0.6 s between swings;
  winded after 3 swings for 3 s; 1.5 times damage winded; the finisher only in that window, taking 0.05 of his max (34,
  51 winded; it was 0.15, which with parry-finishers made him a thug with more health); a parry staggers him 0.6 s;
  stagger threshold 40; resists the heavy's knockdown; walks at 380.
- Phases: "Crew" (he watches, out of the fight; four of his guys: two bats, which are candy canes
  (`BP_Thug_CandyCane`), fists, a shield; `beat_crew`), "Tomas" (opened by the crew going down; 1.5 s protected
  transition; guard 0.2; a 2.5 s beat over her shoulder, his "knees" line; `beat_tomas`), "Santa" (under 0.4 of his
  health; 2 s protected transition; guard 0.2, broken by the confetti arrow (a Smoke effect) within 300 cm; the Santa
  comes loose and catches him too; a 3 s high beat, "Nobody tied down the Santa?" and Ivan's line; 3 confetti arrows;
  `survive_santa`, which completes once he is down and the after lines are over). The blow that puts him down: a 2.5 s
  low, close beat on him ("Good try, me"). A finisher that ends a phase or him: time 0.25 for 0.8 s, camera 110 cm in.
- Three presents (`SM_Kit_Present`) sit in the Santa's lane at 0.3, 0.55 and 0.8 of it; it shoves them along. A putty
  arrow (her quiver carries putty into the chapter) on it or its tether pins it 4 s; the lap does not use it.
- The Santa: waits 2 s, then telegraphs 1.5 s, sweeps 2.5 s, rests 3.5 s; 20 damage (12 / 20 / 28), once per 2 s,
  240 cm either side of its lane and 160 cm along it, a 700 cm/s shove.
- The lane's width comes from his reach (2026-11-10; it was 170 and the lure caught him in one lap run of four): half
  width = where he stops to let a swing land (`MeleeCloseDistance`, 120) + his longest lunge (the haymaker's 60) + her
  capsule (35) + 25 (`_arena.SANTA_HALF_WIDTH`, `create_boss.HAZARD`). So with Kate just outside the lit strip, Tomas
  closing on her stops inside it, and waiting on his 300 cm ring he stands across its middle. `Hawkeye.Lap.SocialClub`
  stands her 60 cm outside its edge across from him while it telegraphs and asserts the sweep catches him at least once.
- The confetti arrow (`DA_Arrow_Confetti`, slot 5, the smoke arrow's slot, which Act 1 never fills otherwise): a smoke
  effect retuned by data (`BP_Effect_Confetti`, a child of `ASmokeCloud`: 350 cm for 2.5 s, the wrapping-paper burst
  as its look), so thugs in it are blinded for a moment; no damage, cap 3, not recoverable. Kate's first one loosed
  in the Santa phase is Tomas's "Oh, that's nice" line.

### Chapter 3's boss, Santa (`DA_Boss_CH03_Santa`; numbers on Normal; 2026-11-10)
- In L_Int_Grotto's workshop (the arena's outline is the room, its floor at 0): 640 health (560 until 2026-11-11, when the scripted player beat him in 41 to 43 s, under the 45 to 70 s target; 320 before the
  2026-11-10 difficulty pass, when the scripted player beat him in 25 s), 1.15 times a thug,
  `BP_Boss_Santa` (BP_Thug in a red suit with white trim, a Santa hat and beard, a sack in his bat slot drawn at 0.85),
  no light riding on him (the workshop's tubes light him; a rim light washed his red out). One phase, "Santa" (`beat_santa`), fighting from the start: a sack swing (14,
  0.8 s wind-up, parryable), a second (14, 0.7 s), an overhead slam (20, 1.1 s, unblockable: dodge it, a 180 cm shove);
  0.6 s between (0.7 before); winded after 4 (3 before) for 3 s, 1.5 times damage winded, the finisher only then, 0.08
  of his max (0.12 before); guard 0.3 (0.35 before);
  walks at 340. Met at `open_vault` (her feet in the workshop): "Ho, ho- oh, no." (`seq_ch03_santa`, at most 20 s), then
  the fight; down: a 2 s beat on him, Kate's "You're a mall Santa..." (`seq_ch03_santa_down`), and `beat_santa`.

## Hands and the thugs' pistol (`UWeaponComponent`)
Kate shoots with `UBowComponent` (see "Bow and arrows"). `UWeaponComponent` is left for two users:
Kate's `Hands` punch while she owns no bow, and the gunner's pistol. Her component is set to
`DA_Weapon_Hands` by `UInventoryComponent`, so `bHasWeapon` is false there and `Fire()` is a punch.
- Hands (`DA_Weapon_Hands`, a `UWeaponDefinition` with `bIsMelee`): 15 damage, a 20 cm sphere swept
  120 cm from the owner's eyes (never the camera), 0.6 s between punches, no ammo. The hit is
  `ApplyMeleeDamage`, so it staggers. A heavy's shield blocks it from the front. `OnHit(HitActor,
  Damage)` fires when the sweep lands on an actor with a health component. With a bow in the
  quiver, left click draws instead (`Input_FirePressed`); the strike on V is the melee system ("Melee and
  thugs").
- The pistol's shots trace on the `Weapon` channel (`ECC_GameTraceChannel1`, default Block), never
  `Visibility`: the engine's Pawn and CharacterMesh profiles ignore Visibility, which is why shots
  passed through characters in the first playtest. Every damageable character blocks `Weapon` on
  both its capsule and its mesh. The capsule guarantees the hit; a second trace against the mesh
  recovers the bone for headshots (`head`, `neck_01`, times 3). `bTraceComplex` stays false.
- The gunner's pistol (set in `AThugCharacter`): 12 damage (times the difficulty's thug damage),
  magazine 12, reserve 120, 300 rounds a minute, range 4000 cm. The burst clock, not the fire rate,
  sets his rhythm ("Thug variety and squad alert").
- `Fire()` does nothing if `bHasWeapon` is false, if reloading, or if less than `1 / FireRate`
  seconds have passed since the last shot. With `CurrentAmmo == 0` it plays the empty click
  (`OnEmptyClick`) instead. A gunner whose magazine is empty reloads instead of firing.
- `Reload()` does nothing if the magazine is full, if reserve is 0, or if already reloading. After
  `ReloadSeconds` (2) it moves `min(MagazineSize - CurrentAmmo, ReserveAmmo)` rounds from reserve to
  magazine. Firing during a reload is blocked; Kate's sprint cancels one.
- `OnAmmoChanged(Magazine, Reserve)` fires on every change.
- Range: a line trace of `Range` units from the owner's view point (a thug's eyes). Past range, no
  hit. The shot direction is scattered in a cone of `HipSpreadDegrees` (2.5), or `AimSpreadDegrees`
  (0.5) while `SetAiming` is on; the gunner's own aim cone is separate ("Ranged thug accuracy").
- There is no damage falloff over range.

## Takedown
- Valid target: an actor with tag `Thug` that implements `ITakedownable`, within `Range` (150 cm),
  and the vector from target to player, projected on the ground, is within `MaxAngleDegrees` (60)
  of the target's backward direction.
- A thug whose alert state is Alerted is not a valid target (`CanBeTakenDown` vetoes it), unless
  `bAlertedThugsAreValid` is on. Suspicious is fine. This is the stealth reward: you get the
  takedown if they haven't seen you yet. The chase's thief refuses one until he is cornered
  ("Chases").
- The player is locked out of movement and firing for `TakedownSeconds` (1.2) while the takedown
  plays. Thugs can still hurt her during this.
- The body going down makes a noise of loudness 0.6 (`TakedownNoiseLoudness`; see "Guard AI states").
- `OnTakedownPerformed(Target)` fires once at the start of the takedown.

## Look input
- Look input is multiplied by the sensitivity from `UHawkeyeSettingsSubsystem` (default
  0.2, clamped to [0.02, 1.0]) and, while aiming, by `AimLookMultiplier` (0.7) as well.
  The character's own `LookSensitivity` property is only the fallback when no game
  instance subsystem exists (tests). Changing the setting takes effect immediately.

## Controller (Xbox layout; PlayStation pads map to the same keys)
| Control | Action |
|---|---|
| Left stick | Move. Radial dead zone 0.2. Light deflection walks, full deflection runs |
| Right stick | Look. Dead zone 0.25, eased (value^1.5) for fine aim, scaled by frame time at 180 deg/s yaw and 120 deg/s pitch times controller sensitivity; halved while aiming |
| A | Jump, ledge climb |
| B | Crouch hold, dodge tap, slide while sprinting, drop to hang at an edge |
| X | Strike (tap light, hold heavy) |
| Y | Takedown when a target is valid, otherwise interact |
| L3 (press left stick) | Sprint: one press toggles it on (the Sprint setting can make it a hold) |
| R3 (press right stick) | Send the partner to the point under the reticle (T on keys) |
| Right trigger | Draw and release |
| Left trigger | Aim |
| RB | Grapple |
| D-pad up / down | Tap: standard arrows / grapple arrows (on release). Hold 0.4 s: the world map / the phone. Left / right cycle slots |
| View | Inventory (tap), quiver wheel (hold); so the map is a hold of D-pad up, M on keys |
| Menu | Pause, on release of a tap (under 0.6 s). Hold 0.6 s: a playtest note, as F12 on keys (still held at 2 s: 10 s of Verbose LogHawkeye). Under a menu it closes the menu on the press, as before |

Playtest capture (2026-09-29; how to use it is in testing.md, "Playtest capture"). Both are read raw by
`AHawkeyePlayerController::InputKey`, before Enhanced Input, so no input asset changed:
- **Note:** F12, or Menu held 0.6 s. Every press logs `LogHawkeye: NOTE #<n> at <s>s: pos=(x,y,z) yaw=<deg>
  state=<HawkeyePlaytest::DescribeState>`, queues `note_<n>.png` with the HUD on (the "Note 3 saved" toast goes
  up once it is written, so it is not in the shot) and rewrites `notes.json`. F12 works anywhere, menus and
  pauses included; the Menu hold only in play (the world running), because under a menu Menu closes it. The
  Menu hold is why a pad's pause now opens on release. F12 or Menu still down 2 s after the press: LogHawkeye
  at Verbose for 10 s, logged when it starts and ends. `Hawkeye.Playtest.*` test the record, the JSON, the
  counters and the state line.
- **Photo mode:** F11 on keys; on a pad, Photo mode in the pause menu (View is the quiver's tap and hold, and
  Menu's hold is the note, so the pad has no free button for it). Refused, with a toast and a `Photo mode: not
  now (<why>)` line, under a menu, in a flashback, in a chapter beat (the close-up, the end sequence, the title
  card), downed, dead, reloading, or with the quiver wheel open. It pauses the world, hides the HUD and flies a
  camera from where Kate's camera was: WASD or the left stick, mouse or the right stick (eased, 120 / 80 deg/s),
  E / RT up and Q / LT down, Shift held or L3 clicked for 4x (500 cm/s base; RT is up, so it cannot also be the
  speed), wheel or LB / RB for the field of view 40 to 110 (5 degrees a notch, 40 deg/s held), R or Y back to
  Kate's view, F12 or A for `photo_<n>.png` at the window's resolution with no UI at all (the corner card
  included), Esc, B or F11 back. It never goes more than 50 m from where it started and does not collide. While
  it is on, every key and axis goes to it and nothing reaches the game; leaving flushes the held keys, puts the
  view target back, unpauses, and changes nothing else (no save, no sprint-toggle stop, no pause menu).
  Alt+Enter still toggles fullscreen; `bF11TogglesFullscreen` is off in `DefaultInput.ini`.

Stick up looks up by default (a double negation once inverted it; fixed 2026-09-27). `bInvertMouseY`
and `bInvertStickY` toggles live in Settings. Mouse look uses `LookSensitivity`; stick look uses `StickSensitivity` (default 1.0, clamp
0.2 to 3.0), both in the settings save. On-screen key hints switch to gamepad names when
the last input came from a pad.

## Settings
- `UHawkeyeSettingsSubsystem` (game instance) owns `FHawkeyeSettings` and persists it in the
  save slot `HawkeyeSettings` on every change. Missing or version-mismatched data yields
  defaults, never a crash. The struct holds mouse and stick sensitivity, the invert toggles, four
  volumes (master 1.0, sfx 1.0, ambient 0.8, music 0.6), difficulty, the accessibility options, the
  time of day, aim assist, sprint mode and the hints toggle; every new option goes in the same struct
  with a version bump. Version 10 is current; 5 to 9 migrate (see below), older ones yield defaults.
- The Settings screen is reached from the pause menu. The game stays paused while it's
  open. Escape in Settings returns to the pause menu, not to the game. Back does the same.
- Sliders apply live: the value changes the game before the player leaves the screen, so
  they can feel it after resuming without a second trip.
- Time of day (built 2026-09-28): Night (default, the story's) or Day, a choice row under
  World in Settings, saved with the rest (settings version 6; a version 5 save migrates and
  comes up Night, anything older still yields defaults). `UTimeOfDaySubsystem` relights the
  district live on the change and at map load. Night is the level exactly as generated and
  is never touched on the default; Day is one row of its table (`GetPreset`), a clear winter
  afternoon (retuned 2026-09-29 from a first day that read blue and dim): the Moon light becomes
  a 16 lux sun, warm white with a little gold (1.0, 0.86, 0.68), 30 degrees up from the
  south-west; sky light 8.0 (from 3.0) tinted warm (1.0, 0.90, 0.80) against the blue sky it
  captures, so shade is blue but not black; fog 0.0015, slightly warm (0.18, 0.16, 0.13),
  starting 30 m out; PP_Global's exposure bias +2 to -0.3 (exposure stays pinned); the
  purple/cream grade neutral, vignette 0.2, stars hidden. Sunlit snow does not clip in
  day_street.png or day_park.png. Street lamps, their heads and the lamp buzz are off. Glows
  get the row's `GlowScale`, 3 by day (2.3 EV under the night makes every emissive about 5x
  dimmer): the level's M_Emissive materials (objective beacons, safehouse doors, the chapter-end
  arrow) by a swapped copy, lit windows at a quarter times that, and every glow a class sets
  through `UTimeOfDaySubsystem::SetGlow` (pedestal cap and icon, target faces, checkpoint rings),
  rescaled on every change. City_Ambience crossfades from the night street bed to the day bed
  over 2 s (the row's `DayBedWeight`, 0 at night and 1 by day; see "Audio"; this replaced the
  night bed turned down to 0.6 on 2026-09-29). Snow falls in both, unchanged (the snowfall component has no density
  setting). Going back to Night restores every recorded value exactly. Interiors and flashback scenes (no outdoor weather) and anything
  tagged Interior are left alone. `-TimeOfDay=Day|Night` overrides without saving, like
  `-Difficulty=`; the `hawkeye.TimeOfDay Day|Night` console command overrides both, also
  unsaved, until `hawkeye.TimeOfDay Saved` or the player changes the row. The engine's
  SunPosition plugin is the route to a real cycle later (more rows, blended by the clock).
- Hints (built 2026-09-29): an On / Off choice row under Controls, after Sprint, saved with the rest
  (`bShowHints`, settings version 10; a version 5 to 9 save migrates and comes up On). Off hides the line
  at once and nothing waits; what she does is still counted learned. See HUD, "First-time hints".
- Lamps and the EMP share the lamps without fighting: the time of day owns each lamp's
  intensity, head glow and buzz; the EMP owns its lights' visibility. A lamp the EMP has dark
  keeps its head unlit and its buzz off through any time of day change, and when the outage
  ends it comes back as the time of day then in force says (off by day, lit at night). Before
  the first change the EMP restores what it recorded, as it always did.
  (`Hawkeye.TimeOfDay.EmpAndLampsDoNotFight`, `Hawkeye.TimeOfDay.GlowScalesByDay`.)

## Inventory and hotbar (built 2026-09-26; the weapon slots went with the pivot)
- `UInventoryComponent` holds what Kate carries: a bow (or none), a seven-slot quiver and a keycard
  ring. The slot, count and cap rules are in "Bow and arrows". The hotbar (`UHawkeyeHotbarWidget`) is
  the quiver along the bottom: seven boxes, each with its number key, the arrow's short name and its
  count (a plain number for standard arrows, "count/cap" for a trick arrow, an infinity for the
  grapple), the active slot bright and an empty slot dimmed.
- Hands are the fallback, not a slot: with no bow owned, left click punches (`DA_Weapon_Hands`; see
  "Hands and the thugs' pistol"). With a bow, left click draws. Takedown (F) is unchanged and works
  either way when the target is valid.
- A slot is empty until its arrow type is picked up or granted. Selecting an empty slot does nothing.
- Inventory (Tab, tap) lists the bow, the quiver slot by slot with its count, and keycards by id.
  Read-only. Opening it pauses the game like the pause menu does.
- Quiver and bow reset with `Clear()` to the mission's grant (`StartingBow`, `StartingArrows`) on a
  mission restart, and at mission complete unless the chapter returns to roaming
  (`bReturnToRoamingAtEnd`; Night Shift does, so she keeps her quiver). Nothing else carries
  between missions.
- Weapon stats live in data assets: `UWeaponDefinition` (Hands and the thugs' pistol) and
  `UBowDefinition` and `UArrowDefinition` for the bow. A new weapon is a new asset.

## Navigation
- Nav data is generated at runtime (`RuntimeGeneration=Dynamic`) and the game mode calls
  a build at BeginPlay if the map shipped with none. Maps are generated headlessly and no
  one presses Build Paths, so this is what makes guards able to move at all. The smoke
  test asserts a navmesh exists and at least one guard is moving after a few seconds.
- A thug's move goal is snapped to the nearest navmesh point: within 2 m for most moves, 3 m for
  cover, the circle round a downed Kate, a noise he walks to and Kate herself when following her
  fails (hanging off a ledge, mid-zip, on a sill, down against a wall). Cover points without a
  complete path from him (the next roof, past a locked door) are not cover. With nothing to snap to,
  or no path, he holds position (a noise he cannot reach he watches from where he stands, and the
  investigation clock runs) and says so at Verbose. The only move warning left is a level with no
  navigation data at all; a bare test world with no navigation system logs at Verbose.

## Melee and thugs (built 2026-09-26)
- Kate's strike is on V: tap for a light (15 damage, lands at 0.1 s, 0.3 s total, 120 cm
  sphere sweep, 20 cm lunge); hold 0.4 s for a heavy (35 damage after a 0.6 s wind-up,
  40 cm lunge) that knocks the thug down for 3 s, after which he blends from the ragdoll
  back to standing over 0.4 s (not yet seen in a full-length in-game fight). Takedown from
  behind on F is unchanged.
- A thug's bat hangs by his leg while walking, rises level behind him over the 0.6 s
  wind-up, swings across in 0.15 s, and drops back.
  Without clips, strikes are procedural lunges; with them, see "Combat animation clips".
- Dodge: tap Ctrl while moving and not sprinting dashes 300 cm in 0.4 s, invulnerable for
  the first 0.25 s, cooldown 0.8 s. Ctrl held crouches; Ctrl while sprinting slides.
- Thug melee: `EThugWeapon` is Fists, Bat, Pistol (the gunner), Bow (the archer) and Shield (the heavy); fists and bat rush in. Alerted and within
  250 cm they rush at 450 cm/s, close to 120 cm, and swing after a 0.6 s wind-up (a log
  line and a glowing mask) for 15 (fists) or 25 (bat) with a stagger; cooldown 1.2 s;
  after two swings they back off 2 m. Hitting a thug during the wind-up cancels the swing.
- Feel: 2-frame hit stop (time 0.1 for 0.033 s) on melee hits (4 frames on a landed heavy since
  2026-09-29, see "Combat readability"), camera shake when Kate is hit, a 0.1 s flash on a hit thug,
  and desaturation plus vignette under 40% health.
- Tracksuits: red suit, white stripes, black ski mask, all from one material on the old
  mannequin's single slot. No flashlight. Sight and hearing unchanged.
- District placements, left from the retired Rooftops chapter (the Night Shift chase has none of its
  objectives, so they complete nothing): a roof pair (fists, bat) on the `cross_block` roof tagged
  RoofPair; a street pair (bat, gunner) patrolling 40 m of the Avenue A sidewalk by the park. Verify
  projects every thug's feet onto the navmesh.

## Thug variety and squad alert (built 2026-09-26)
- Thugs run `ST_Thug`, a headless-built StateTree with states Stunned, Reposition, Cover,
  Attack, Investigate, Patrol in priority order; every thug runs it, tests and C++ spawns
  included (architecture.md, "Thugs"). Cover points come from `EQS_CoverPoints`, an EnvQuery authored in C++ (never
  save it from the editor), with the C++ candidate ring as fallback.
- Gunner: 0.8 s telegraph (pistol raised, glint, log line), then a burst of 3 shots of 12
  damage 0.25 s apart, scattered in a 6 degree base cone ("Ranged thug accuracy"). Takes cover after a burst or when aimed at
  within 6 degrees: nearest blocked point within 800 cm on his level, hides 1.2 s, peeks,
  new cover every 6 s. Backs off to 600 cm if the player closes inside 300 cm. EMP jam
  holds fire. Any hit breaks the burst.
- Archer (`BP_Archer`, `EThugWeapon::Bow`, Barney's people): 1.2 s draw with a purple
  glint, 30 damage, 5000 cm/s, leads the chest (or the centre when she crouches, so a
  parapet covers her); keeps 1500 to 2500 cm, zips to an anchor when the player closes
  inside 800 cm, aggressive only within 3000 cm with a clear line; any hit breaks the draw.
  Arrows leave from cheek height. His purple arrows are picked up as standard, with a
  one-time "Trickshot's arrow" toast. Two on the roofs facing `find_arrow` (OSM W248142394
  and W248142314), tagged ArcherPair; their sightlines are kept clear of rooftop clutter.
  The scripted duel shows a ducking player breaks every draw; archers may need to hold a
  draw briefly after losing sight.
- Squad alert: 1.5 s after a thug goes Alerted, thugs within 1500 cm with a line to him
  turn Suspicious toward the player's last seen position.
- Heavy (built 2026-09-27): `BP_Thug_Heavy`, 200 HP, a 60 x 110 cm riot shield on the left forearm blocking arrows and light strikes in the front 120 degrees (arrows stick in it); staggered only by a heavy strike, bola, or explosive; putty holds him; shield bash with a 0.8 s telegraph, 30 damage, 250 cm knockback, plus a slow bat swing; walks at 300. One on his own 20 m patrol by the park corner, tagged StreetGroup. For the music he is a Fight, not a Duel, unless two or more others are alerted with him (see "Audio").
- Archer holds (built 2026-09-27): on losing line of sight mid-draw he holds up to 2.5 s and fires within 0.2 s of the player reappearing in his cone; a 0.6 s loose window after a shot leaves him open. Scripted duel: won with 1 hit taken.
- Readability: a cream "!" for 0.6 s over a thug going Alerted, "?" for Suspicious; a thin health bar over damaged thugs within 1500 cm, fading after 3 s (2026-09-29: a larger red-orange "!" while he winds up a strike, see "Combat readability"). Scripted street fight against bat, gunner, heavy: won, 0 hits, 0 untelegraphed hits, fairness metric 0% staggered time.

## Guard AI states
Thugs are the guards (`EThugAlertState`, `AThugAIController`). Perception runs on a 0.25 s think.
- `Calm`: patrol. Sight radius 1500 cm (lose sight at 1800), half-angle 35 degrees, hearing range
  1200 cm (`SightHalfAngleDegrees`, `HearingRange`).
- `Suspicious`: heard something or saw something briefly. Walks to the stimulus,
  waits `InvestigateSeconds` (4), returns to `Calm`. Takedown still valid. Seeing her for
  `SightConfirmSeconds` (0.6) makes him Alerted.
- `Alerted`: has confirmed the player. Attacks, seeks cover, alerts the squad after
  `SquadAlertDelay` (1.5 s, radius 1500 cm). Returns to `Suspicious` after `LoseTargetSeconds` (5)
  without perceiving the player, then to `Calm`. A noise of loudness 2 or more
  (`GunshotLoudnessThreshold`) alerts at once; being hit, and a crime that starts alerted, report one.
- Noise: the player moving on the ground emits a noise event every 0.5 s (`NoiseIntervalSeconds`):
  sprinting loudness 1.0, walking or running 0.4, crouching 0, standing still 0. Takedowns 0.6 (the
  body drop). Heard within `HearingRange` times loudness, with walls in the way ("Interiors").
- Thugs animate from `IdleAnim` below 20 cm/s ground speed and `WalkAnim` above (and a run clip for
  the melee rush), switched only on state change. They carry no flashlight: the sight cone has no
  visual of its own, and the "!" and "?" glyphs are the read ("Readability").
- Guard death logs the killer and cause at Log level (takedowns name the attacker);
  every bullet hit logs actor, bone, and damage at Verbose so a playtest can be
  reconstructed from `Hawkeye.log`.
- (2026-11-11: the ragdoll is now thrown from the blow that killed him and settles; see "Contact, reaction and knockout".)
- Guard death is always visible. `GoLimp(Killer)` ragdolls the mesh (the mannequin's
  physics asset from the High feature pack; the Standard pack's copy is an empty stub and
  must never be used). If the mesh reports it is not simulating afterwards, a procedural
  collapse runs instead: 0.6 s tip of 85 degrees away from the killer, 20 cm drop,
  animation stopped. Either way the capsule stops colliding.
  Which path ran is logged once per guard at Warning.

## Interiors: enemies and the AI indoors (built 2026-09-29, for chapter 4's auction house)
Tested by `Hawkeye.Interior.*` (InteriorAITest.cpp, the rules and the sample in a real world) and played by
`Hawkeye.Lap.InteriorStealth` and `Hawkeye.Lap.InteriorLoud` (InteriorLapTest.cpp, standalone). The layout
format is in `claude-docs/asset-conventions.md`, "Interior layouts".
- Enemies come from the layout's `enemies` list (type, room, start, facing, patrol, wait, alert group) and
  `keycards` (who carries which), placed by `generate_interior.py` as BP_Thug, BP_Archer or BP_Thug_Heavy with
  TargetPoint patrols; `verify_interiors.py` checks each starts on the navmesh, every patrol point is on a path
  from him, and no capsule of his (start or any point) is inside a wall, a prop or a doorway. The sample has
  four: a fists thug walking the lobby into the office (crew `front`), a bat thug on the hall floor, a gunner
  at the gallery rail and an archer on a beat by the vault door who carries its keycard (crew `hall`).
- A carried keycard is a placed pickup in the thug's `CarriedPickups`: hidden, without collision and not
  takeable until he goes down (takedown or fight), then dropped at his feet on the loot fan.
- Eyes follow the body (everywhere, fixed 2026-09-29): perception's sight looks along the thug's body yaw,
  pitched by his `LookPitch`, while he is Calm or Suspicious, and along his aim once Alerted. The thug
  controller never ticks, so its control rotation (which the cone used to follow) stayed where he spawned
  facing; a patrolling thug looked the same way all his life.
- A patrol point tagged `PatrolFacing` (a layout point with a yaw, and optionally a pitch) is one he turns to
  while he waits there, his gaze pitched by it. The gallery gunner waits facing the hall at -25 degrees: with
  the 35 degree cone round a level gaze he could not see the floor under a gallery at all.
- Sight goes through an open doorway and over a gallery rail; walls, shut door leaves and glass block it. From
  the rail (50 cm back) the gunner sees the hall floor from 2.5 m out; 150 cm back from the rail he would see
  almost none of it, which is why his beat runs along the rail.
- Hearing has walls (`HawkeyeThugHearing`, the engine's hearing is a sphere through anything): a noise is heard
  within HearingRange x loudness when the straight line to his head is clear, or the line 1.5 m above both
  ends (over a balustrade, a counter); otherwise along the navmesh path from the noise to him, within the
  range, a quarter of it when that path crosses a shut door; with no path, not at all.
- A takedown makes the body-drop noise at the body (0.6, so 720 cm): in the office with its door shut, not
  heard in the lobby past the door nor in the hall behind two walls; with the door open, heard in the lobby.
- A thug's squad alert reaches only his crew (`AlertGroup`; a thug with none reaches only thugs with none),
  within 1500 cm with a line to him, as before. An interior is its own map, so nothing leaves the building.
- An indoor archer (`bArcherSeesAllRound` off, set by the generator) sees only inside his sight cone while
  Calm, like any thug; Suspicious or Alerted he sees all round as on the roofs.
- An unlocked door opens for a thug who walks into its zone (patrols go through doors); doors do not close.
  While a door is locked and shut its nav link's area is Null (left out of the navmesh) and its leaf cuts the
  navmesh (a 100 cm doorway is not always eroded shut: with the crate moved off the vault's threshold a strip
  ran under the leaf), so no path, and no thug investigating a noise, goes through it. Opening it puts the
  link back (Default), lets the leaf go and rebuilds the tiles.
- Cover indoors stays on the floor he stands on: EQS and ring candidates count only where they project onto
  the navmesh (the flat donut round a gallery gunner used to put cover points in the air past the rail).
- Thugs take the stair between floors (the sample's bat thug, hall floor to gallery by the lobby, the stair
  and its upper door, which opens for him).
- Ragdolls and knockdown ragdolls use CCD: a thug thrown at a 20 cm wall at 1500 cm/s (a blast, a death
  throw) or knocked down by a heavy stays in his room.
- Senses coming back on (thinking re-enabled, a challenge over) take what perception already sees at once,
  not at its next change.
- The fight camera is still capped by `IndoorArmLength` (250 cm) indoors; the loud lap measures the longest
  arm seen while it is up.
- Measured (standalone, 2026-09-29): `Hawkeye.Lap.InteriorStealth` in 57.2 s with 0 alerts, 0 suspicions,
  4 takedowns and 0 hits (the gunner taken when neither the bat thug nor the archer would hear the body);
  `Hawkeye.Lap.InteriorLoud` won in 40.1 s, all 4 alerted, 2 hits taken (24 damage, the gunner's), 18 arrows,
  7 lights, 1 heavy, 1 finisher, the fight camera's arm held at 250 cm. The "Kate stood up by herself a moment into
  a creep" of that run was the lap's: it held the crouch key and let go of it to stand, which with the default
  toggle does nothing, so she crept on from the lobby up the stair, and the next creep's press toggled her up.
  The laps now press crouch as a player does and fail on any crouch that ends without a stand-up press (see
  "Movement", crouch). Re-measured after the fix: the stealth lap in 46.6 s, 0 alerts, 0 suspicions, 4
  takedowns, 0 hits, 3 stand-ups all asked for; the loud lap won in 29.3 s, all 4 alerted, 0 hits, 4 arrows,
  11 lights, 1 heavy, 1 finisher, the arm held at 250 cm.
- Seen in `Hawkeye.Screenshot.Interior`: `int_patrol.png` (the bat thug walking his beat through the chairs,
  from the archway), `int_takedown.png` (the lobby thug going limp in front of her), `int_gallery_gunner.png`
  (re-framed 2026-09-29: from the gallery, 4.8 m along it, the gunner at the rail turned to her, whole and lit; his
  pistol is a glint at his hand rather than a shape. From the stage 9 m off he was small and washed pale by the
  pendants over the hall; from the hall floor under the gallery the parapet hides all but his head),
  `int_vault_open.png` (the vault doorway open on its shelves and lit case).

## Save data (built 2026-09-26 on SPUD)
- SPUD (MIT, vendored at `Plugins/SPUD`, commit 12a30da) persists the world; it built on
  5.8 unchanged. One slot, `HawkeyeCampaign`; automation runs use `HawkeyeCampaignAutomation`.
- Saved: Kate and Clint (transform, controller rotation, health, bow, quiver slot by slot,
  active slot; components are mirrored into save-tagged fields before a save and restored
  after a load; the grapple slot's count is written but ignored on load, so a save from before
  the grapple went unlimited, even one at 0, loads with the grapple ready), thugs (position, dead flag; a dead thug goes down again on load without
  dropping loot twice), campaign state (`UHawkeyeCampaignState`, version 1: mission path, completed objectives,
  safehouses found and the last one used, noticed places, play time, which character was controlled,
  the phone's received, read and pending messages, seen chapter titles, played dialogue sequences,
  challenge bests and completions, crime counts, hints shown and learned, a running chase's
  route, progress and beat flags, what a boss fight has said, the campaign flags and the running chapter's fired events;
  Kate's keycards since 2026-11-10). Not saved: stuck arrows,
  pickups, EMP lamp state, spawner-created actors, the partner's fight state.
- Autosave on objective completion, safehouse entry and visit, fast travel, a finished challenge or
  crime, entering an interior or a playable scene, new game, quit to menu, and after 60 s of roaming
  (the clock runs only while the player is alive, on the ground, and no thug is alerted; a
  fight pauses it). Death (the down running out, see "Health and damage"), or Restart,
  fades to black and loads the last save; with no save it reloads the level.
- Continue reads the header first: a version mismatch starts a new game with a warning; a
  missing mission asset (a save that names the deleted `DA_CH01_Rooftops`, say) starts a new game with
  an error. Never crash on stale data. The migration hook (`CanMigrateFrom`) exists and has no
  migrations, so only the current version loads.
- Safehouse: `City_Safehouse` on 140 East 7th Street (OSM W248142338) facing the park. E
  heals to full, marks it found, autosaves, and opens Refill arrows, Save, Fast travel,
  Chapter select (a stub), Leave.
- Main menu overlays the paused district on first boot: Continue (when a save exists),
  New Game, Settings, Quit. The pause menu has Quit to menu.
- Quit to menu saves first (reason "quit to menu"; refused and logged, as any save is, while she is
  down, in a playable scene or mid-load), so Continue brings back that moment: the thugs where they
  stood, the quiver, her health, the hints, the phone (a text still on its delay arrives on
  schedule). Before 2026-09-29 Continue went back to the last autosave, with the street patrol 8 to
  16 m from where the player left it and her health from the last objective (`Hawkeye.Lap.Campaign`).
- Interaction now also works when the player stands inside an interactable's zone (the
  camera trace alone couldn't reach from 350 cm behind her).

## Audio (built 2026-09-27, all synthesized, nothing downloaded)
- 45 MetaSounds (37 one-shots, 8 loops) under `/Game/Audio`, authored headless by
  `Tools/Editor/create_audio.py` through the MetaSound builder API. Nodes: Sine, Saw,
  Noise, state-variable and one-pole filters, AD envelopes, Multiply/Add/Subtract,
  RandomFloat, LFO, Trigger Delay/Repeat. Every sound has a `PitchVariation` input; the bow
  creak takes `Draw`, the zip hum `Speed`, the landing `Intensity`. A changed recipe
  deletes and rebuilds the asset at the same path.
- Wired: bow draw loop, release, whistle, impacts by surface (wood, stone, flesh), pickup;
  grapple fire, zip loop, landing; footsteps every 70 cm walking and 55 cm sprinting, a
  landing scaled by fall height, roll, vault and mantle effort; melee hits and heavy hits;
  thug telegraphs, bat swing, hurt, death, shield block, stagger; gunner shots; every
  trick arrow effect with the smoke hiss looping for the cloud's life; menu hover and
  click, objective chimes and toasts.
- Ambience: `City_Ambience` crossfades street hum to rooftop wind above 10 m. The 20 lamps
  nearest the park carry a 120 Hz buzz that the EMP silences with the light and that is off
  by day with the lamps. Two street beds (2026-09-29): `MS_Amb_Street` by night (the mains
  drone, a traffic rumble, far horns) and `MS_Amb_StreetDay` by day (wind through the block,
  a wider and brighter traffic hum with tyre hiss, a far horn every 20 to 50 s, no mains
  drone). Both loops run; the time of day's `DayBedWeight` (Night 0, Day 1) splits the street
  level between them, `night = street x (1 - w)`, `day = street x w`
  (`HawkeyeAudioMath::ComputeStreetBeds`), and a change of the weight fades over 2 s instead
  of the usual 1 s (`Hawkeye.Audio.DayBedSelection`). The day bed's path is a C++ default on
  `AHawkeyeAmbience`, so the generated map needs no rebuild for it.
- Music (built 2026-09-29, a first pass for feel, not a score): `UHawkeyeMusicSubsystem`
  (world) plays `MS_Music_Score`, one looping MetaSound that holds every layer on one clock
  (90 bpm, D minor), and sets its five gain inputs every frame: `PadGain` (a low D minor pad),
  `PulseGain` (a low D pulse on each beat, a ghost on the off-beat), `PercGain` (an off-beat
  tick and a thump on 2 and 4), `MotifGain` (two notes, D3 then A3 a beat and a half later,
  every two bars), `DroneGain` (a dark low drone). One graph instead of separate loops keeps
  the layers sample-locked without the engine's Quartz clock (Quartz only earns its keep with
  separate sources; not used). States, from the thugs, the player, the crime and the
  challenge subsystems:
  - Roam: nothing, or the pad at half at night or while a challenge runs.
  - Alert: one thug alerted (or a crime on within 40 m of Kate): the pulse.
  - Fight: two or more alerted, or an alerted one within 8 m: pulse and percussion.
  - Duel: an alerted archer or anything with a boss phase component, or the heavy with two or
    more other thugs alerted beside him (2026-09-29): pulse, percussion and the motif. The heavy
    alone, or with one other, is an ordinary Alert or Fight (`HawkeyeMusic::HeavyDuelOthers`).
  - Win: every thug alerted in this fight is down (dead, limp or despawned) and at least one
    went down: all layers out in 1 s under `MS_Music_Win` (a 2 s D major resolve), then Roam.
    A thug alerted during the sting goes straight back to the fight; a crime starting nearby waits
    for the sting to end. Once per fight (2026-09-29): the won fight does not sting again until
    someone new is engaged or it is forgotten (the crime screenshot pass had a crime starting after a
    won fight flip Win and Alert every frame, `Hawkeye.Music.WinOncePerFight`).
  - Downed: Kate is downed: every layer out in 1 s, the drone in. On the revive the music
    goes straight to what the fight is.
  Rising is immediate. Falling waits: the level must be lower for 4 s before it drops, one
  step at a time (Duel to Fight to Alert to Roam), and any rise resets the wait. Layers fade in
  over 1.5 s and out over 2 s (1 s into Win and Downed). A thug counts toward "this fight"
  once he is alerted; the fight is forgotten when the music reaches Roam.
  A pause screen (map, inventory, pause menu and settings, safehouse menu, phone, results card,
  flashback: anything that pauses the world) holds the sound as well as the state machine
  (2026-09-29): the subsystem ticks under a pause only to notice it and pauses the score and any
  win sting (`UAudioComponent::SetPaused`); closing the screen resumes both where they were, in
  the same state. Logs `Music: paused` / `Music: resumed` (`Hawkeye.Music.PausesWithPauseScreens`).
  Every transition
  logs `Music: <from> -> <to>` at Log with the numbers behind it. The graph peaks near 0.35
  of full scale and `SCL_Music` defaults to 0.6 on the slider (0.36 gain), so the music sits
  well under the effects. Rules in `Source/Hawkeye/Audio/HawkeyeMusicRules.h`; tests
  `Hawkeye.Music.*`.
- Sound classes `SCL_Master`, `SCL_SFX`, `SCL_Ambient`, `SCL_UI`, `SCL_Music` and the mix
  `SMX_Settings` drive Master, Sound effects, Ambience and Music sliders in Settings
  (Music added in settings version 9, default 0.6; a version 5 to 8 save migrates and gets
  the default).
  Attenuations: `ATT_World` 300 to 3000 cm, `ATT_Lamp` 50 to 400 cm. Naming: `MS_` for
  MetaSounds, `SCL_` classes, `SMX_` mixes, `ATT_` attenuations.
- Verification: `Hawkeye.Audio.Smoke` plays every sound in a `-game` run and checks it
  plays; every trigger logs at Verbose as `Sound:`. Sound classes and the mix stay loaded
  for the whole run (a map change once spammed thousands of missing-class warnings).
- Open: the animation sample's own footstep events may double Kate's footsteps.

## Effects (built 2026-09-27, engine Niagara templates, nothing downloaded)
- 18 Niagara systems (33 emitters, 7 materials) under `/Game/VFX`, each a copy of one of
  the Niagara plugin's template emitters (`/Niagara/DefaultAssets/Templates/Emitters/`)
  with module settings changed headless by an editor-only C++ builder
  (`Source/Hawkeye/Vfx/HawkeyeVfxBuilder`) driven by `Tools/Editor/create_vfx.py` recipes.
  Python alone can copy templates but cannot edit module values, which is why the builder
  exists. The old placeholder shapes show only if a system is missing.
- Templates per effect: OmnidirectionalBurst for smoke, EMP ring, fireball and explosion
  smoke, putty splat, impact dust, landing snow, bow puff, muzzle smoke (and as a steady
  stream for the smoke cloud's 6 s tail, the bola whirl, chimney wisps); DirectionalBurst
  for every spark, stone chips, wood splinters, footstep kick; SimpleSpriteBurst for
  flashes; Fountain with a ribbon renderer for arrow and bola trails; DynamicBeam for the
  zip line and gunshot tracer; RecycleParticlesInView (GPU) for snowfall, 800 flakes
  following the camera. Scorch decal placed by a ground trace; EMP chromatic aberration is
  a post-process material.
- Chimney smoke on the 6 chimneys nearest the player at load. Frame time 7.4 to 7.7 ms.
- Flash lifetimes (2026-09-29): a `SimpleSpriteBurst` flash keeps the template's own lifetime unless its
  Lifetime Mode is set to Direct Set, and Lifetime Min/Max are then never read; `create_vfx.py`'s
  `flash(..., direct_life=True)` sets it. Every flash uses it: `NS_MeleeSpark`'s core and glow (0.08 s),
  `NS_HitSpark`'s star (0.2 s), `NS_ParryRing`'s flash (0.08 s; the ring itself, 0.22 to 0.3 s, is a burst and
  was always honoured), `NS_MuzzleFlash`'s flash (0.05 s), `NS_AnchorSparks`' bite (0.12 s, 120 cm and a hot
  warm core so it reads from the street 20 m off; a disc round the anchor block, not a fireball), `NS_EmpPulse`'s
  flash (0.14 s) and `NS_Explosion`'s core (0.1 s; the fireball, sparks, smoke and scorch still linger).
  Checked by particle count in the screenshot passes: `arrow_hit_spark_later.png` (0.5 s after the hit,
  nothing of `NS_HitSpark` left), `muzzle_flash_later.png` (0.3 s after the shot, no flash or sparks; the smoke
  puff may stay), `anchor_spark_later.png` (0.5 s after the bite, no bite flash), `parry_flash.png` (0.17 s
  after the parry, no flash, the ring out), `emp_flash_later.png` (0.5 s after the flash tick, `Flash` at 0, the
  ring may remain) and `explosion_flash_later.png` (0.6 s after, `Core` at 0, sparks and smoke may remain) fail
  if a flash is still alive. The `emp_flash` and `explosion_flash` shots stop time 0.05 s in; `vfx_explosion.png`
  is stopped 0.15 s in too, because as a timed capture it was sometimes taken from the editor's own level
  viewport instead of the game's (a night-sky frame with the axis gizmo) once the explosion's core lived only
  0.1 s. `Hawkeye.Screenshot.Vfx` runs the effect shots on their own.
- Open: the explosion still reads washed out (its light plus camera-shake blur); the footstep
  kick is faint. (The arrow's hit spark competing with the thug's hit flash: melee now has its own
  small `NS_MeleeSpark` and the flash dominates, see "Combat readability".)

## Narrative plumbing (built 2026-09-27; placeholder text only, story is written with Cameron)
- Phone (`UPhoneWidget`, P or hold D-pad down 0.4 s): contacts and threads from
  `DT_Messages` (Id, Sender, Text, Trigger: ObjectiveCompleted / ObjectiveStarted /
  ChapterStart / Event, TriggerObjectiveId, TriggerEvent, ChapterId, DelaySeconds,
  bRead, Thread, bDraft). A message arrives when its trigger fires, with a 2 s HUD notification and an
  unread badge. Read state is saved. Contacts group by `Thread` when set, else by Sender: Kate's own
  replies (Sender Kate, Thread Clint, arriving read) sit in the thread they answer (2026-11-10). A row with a
  `CallSequence` is a call (2026-11-10, chapter 3's video call from the farm): when it arrives its DT_DialogueSequences
  sequence plays (the voices as subtitles, nobody on screen; it waits out a fight like any sequence, and plays once a
  campaign), and the row stays in the thread as the call's record ("[Video call from the farm]"). Night
  Shift has seven drafted rows after `catch_thief` (Clint, Kate's replies, the landlord).
- Chapter title card (`UChapterTitleWidget`): number, `OpeningTitle`, `OpeningSubtitle`
  from the mission definition, 3 s hold then fade, skippable, input live. Seen-state saved.
- Chapter end: `AChapterEndInteractable` completes an objective on examine, plays a 2 s
  close-up camera push, then the end card (`EndCardLine` from the mission), then the
  flashback if set, then back to roaming with a "[Chapter complete]" toast. The retired Rooftops
  chapter's was the purple-fletched arrow in `City_ChapterEndTower`; it stays in the district, dark and
  never offered, since Night Shift has no find_arrow (a chase's catch ends that chapter).
  Not examined while a thug is alerted (the district's ArcherPair guards the arrow: examined under them, the
  sequence ran and the return from the scene put Kate back in their sight, down 4 s after the toast)
  or while she is down (the sequence ran with her on the ground and the objective's autosave was
  refused). Down or dead, nothing is interactable: no prompt, and E does nothing
  (`Hawkeye.MissionFlow.ChapterEndWaitsOutAFight`). A player who reaches the arrow unseen can still
  be seen from the return point by archers left standing.
- Flashback playable scene: `PlayableScene` and `ReturnPointLabel` on the definition.
  After the slides the scene map loads; the district is saved first, saves are refused
  inside the scene, and its mission completion returns to `City_SceneReturn_<id>` with
  state restored. `DA_FB00_Placeholder` with three "[Slide N]" slides leads to
  `L_Scene_Placeholder`, a 20 x 20 m room; no chapter plays it now (Night Shift has no flashback).
- `UDialogueSubsystem`: `PlayLine(Row)` shows speaker and subtitle and plays audio if set;
  a queue; `PlaySequence(Name)` from `DT_DialogueSequences` (Sequence, Order, Line,
  GapSeconds) survives traversal and pauses in combat. Banter routes through it.
  `DT_Dialogue` gained Audio, DurationSeconds, and a Scripted situation banter never picks.
  `bDraft` marks a line as a draft for the owner to edit. Night Shift's ten sequences (`seq_ch01_open`,
  `_vault`, `_ahead`, `_grapple`, `_botch`, `_dumpster`, `_climb`, `_cornered` (the thief, then Kate),
  `_caught`) speak `docs/chapters/ACT1-script.md`'s lines, every one flagged a draft.
- Chapter text: Night Shift's title, subtitle, end card line (Kate's last line) and objective titles come
  from ACT1-script.md, flagged drafts (`UMissionDefinition::bDraftText`, `UMissionObjective::bDraft`);
  the toast is still "[Chapter complete]".

## Small rules added 2026-09-27
- D-pad down: hold 0.4 s opens the phone; a tap selects arrow slot 2 only on release.
- Phone messages still waiting on their delay are saved with the time remaining.
- Snowfall runs only when the map's game mode sets `bOutdoorWeather`; the placeholder
  scene room turns it off and pins its exposure two stops down.
- Explosion blast light 400 cd; motion blur off for 0.3 s during the blast.
- A chained zip keeps ignoring the roof it just left until clear of it.

## Melee depth (built 2026-09-27)
- Combos: light, light, light do 15, 15, 25 (the third knocks back 150 cm), each input
  within 0.35 s of the previous light's hit landing or, if it hit nothing, of its hit window
  ending (2026-09-28: punching the air still runs Light1, Light2, Light3; before, a miss put
  the chain back at Light1). A heavy anywhere in the chain does 35 and knocks down, and ends
  the chain, hit or miss. The HUD counter and the x5 bonus count only landed hits: counter
  from x2, clears after 2 s; at x5 it turns purple and adds 20%.
- Parry: a strike tap during a telegraph from a thug within 250 cm and in front staggers
  him 1.5 s with no damage taken, a 4-frame hit stop, `NS_ParryRing`, and `MS_Parry`.
  Works on fists, bat, the heavy's bash and slow swing; cancels only the first shot of a
  gunner's burst; never on archers, never on an unblockable swing (a boss's, see "Bosses"). A boss's parry stagger is
  shorter and opens no finisher.
- Finisher: F on a staggered or knocked-down thug within 200 cm (the stealth takedown is
  tried first). 1.2 s: lunge, camera 40 cm in, time 0.5 for 0.4 s, lethal at 0.35 s,
  thrown ragdoll; input locked, invulnerable. With the bow up it's a sweep. Gate (2026-09-27):
  only a knocked-down thug, a parry-staggered thug, or one hit by a combo ender within
  the last 1.0 s qualifies; ordinary 0.5 s hit staggers do not. Fight times barely changed.
- Hit lean: 5 degrees for 0.2 s away from the hit direction, on thugs and Kate, through
  the IK post-process graph. Strike poses: light punches the right hand out over 0.1 s
  and back over 0.2 s; heavy both hands over 0.25 s; bow finisher sweeps the bow hand.
- Soft lock: dodges turn toward the nearest thug within 400 cm in front over 0.1 s (strikes use
  the melee assist below since 2026-09-28). A dodge during a telegraph within 300 cm gives 0.1 s
  of slow motion.

## Melee assist (built 2026-09-28, from playtest note "punching in their general direction")
- Where she means: the move stick's direction if it was pushed in the last 0.15 s, else the
  camera's forward. A strike (every light of the chain and the heavy) picks the best living thug
  within 350 cm (flat, 200 cm up or down) and 60 degrees of it: the lowest distance / 350 plus
  angle / 60, so one straight down the stick beats a nearer one off to the side. The log names the
  pick ("light assist picks BP_Thug_C_2 at 240 cm, 18 deg off where she meant") or says nobody.
- She turns to him over 0.1 s as before, then keeps facing him (up to 720 degrees per second)
  for the rest of the wind-up if he moves. The blow goes at him at the press.
- (2026-11-11: the 90 cm below is now each attack's contact distance from the region it aims at, capped; see "Contact,
  reaction and knockout".)
- Closing the gap: a root-motion strike clip is warped to 90 cm short of him (the warp target
  follows him through the wind-up; inside 90 cm it only turns her). Without one (procedural, or an
  in-place clip) the lunge grows to end 90 cm short of him, never less than the attack's own lunge,
  never more than 260 cm, and takes the longer of its own time and 0.8 of the time to the hit.
- Kate's strikes sweep a 60 cm sphere (35 before) 120 cm ahead.
- Forgiveness: if the sweep finds nobody, the thug picked at the press is still hit when he is
  alive, within 1.3 times the attack's reach (Range + Radius, 234 cm for her strikes, measured to
  his capsule's edge) and within 30 degrees of the swing's direction when the hit lands (with a
  clip, at any tick of its hit window). So a thug stepping back mid-swing is still hit; one who
  was never a valid pick is not.
- Unchanged: the parry is tried first with its own rules (250 cm, 70 degrees); dodges keep the
  stick's direction and the 400 cm soft lock for facing; thugs' swings get no assist.
- A clip-timed wind-up's clock runs (2026-09-28 fix): `GetPhaseRemaining` counts down while the
  clip's hit window has not opened, so a thug's telegraph elapsed time is right and Hard's parry
  window (not the first 0.1 s) opens on clip swings. Before, it read 0 and Hard never parried.
- Scripted fights after these changes (standalone `-game`, seconds / hits taken / Kate's health):
  Story roof 4.4 / 0 / 100, duel 9.1 / 0 / 100, street 15.3 / 1 / 100; Normal roof 4.4 / 0 / 100,
  duel 8.9 / 0 / 100, street 17.0 / 3 / 100; Hard roof 4.4 / 0 / 100, duel 9.1 / 0 / 100, street
  17.1 / 3 / 63. All won, 0 untelegraphed hits.

## Combat readability (built 2026-09-29, from playtest note "combat isn't super clear at this stage")
Rules in `Source/Hawkeye/Combat/CombatReadability.h` (pure, tested by `Hawkeye.Melee.TargetMarker*`,
`Hawkeye.Camera.FightCamera*`, `Hawkeye.Melee.HitStopAndTelegraphGlyph`); drawn by
`UHawkeyeThugOverheadWidget`.
- Target read: the thug the melee assist picks for a swing that starts gets a thin ring on the ground
  round his feet (his capsule radius plus 12 cm, 2.5 px on a 1 px dark edge so it reads on snow, Kate's
  purple from the palette) and his
  health bar drawn 30% whiter. Full while that swing runs, fading to nothing over 0.3 s after it ends;
  a new pick moves it, a swing at nobody clears it, his death or removal clears it. A knocked-down
  thug's ring follows his body. One marker, Kate's only.
- Telegraph glyph: while a thug winds up a strike a parry answers (fists, bat, the heavy's bash and
  slow swing), the "!" over him is the telegraph's, not the alert's: 36 px bold (the alert glyph is
  20), a 2 px outline, the palette's danger colour (default red-orange 1.0/0.16/0.03 linear; deuteranopia hot
  pink, protanopia orange, tritanopia orange-yellow, each kept apart from that palette's other four
  accents), growing to 1.3x by the hit and pulsing 5 times a second by up to 0.12x (reduce flashing
  scales the pulse to 30%, the growth stays). A gunner's raised pistol gets it only within 600 cm of
  her; an archer never does (his purple glint is the tell).
- Glyph anchor (2026-09-29): the alert "!" and "?" and the telegraph "!" sit on his head bone plus 20 cm
  (`GlyphHeadClearance` on the thug, about the top of his head), not on his capsule's top plus 28 cm, which
  put the "!" about 60 px over his head at close range. The health bar sits on the same anchor (2026-09-29,
  second pass: it had kept the capsule's top plus 28 cm, about 65 px over his head close up, so a hurt thug's
  "!" still rode high). The stack, bottom up: the bar (its bottom on the anchor), then the parry line under a
  telegraph, then the glyph, each `StackGap` (4 px, dark edges included) over the one below; with no bar the
  glyph or parry line sits on the anchor itself. The glyph's gap is measured from its ink (its baseline less its
  outline), not its text box, which runs on below by the font's descent. They never overlap (`HawkeyeCombatReadability::ComputeGlyphBottom`,
  tested in `Hawkeye.Melee.HitStopAndTelegraphGlyph`). A thug with no head bone uses the capsule anchor.
- Parry line: under the telegraph glyph, an 18 x 3 px cream line on a dark edge, drawn only while a
  tap now would parry him: in the difficulty's window (Hard: not the first 0.1 s), within 250 cm and
  70 degrees of her view, 150 cm up or down (`AHawkeyeCharacter::CanParryNow`, the same test the
  parry itself uses).
- (2026-11-11: the hit stop's length is now by the blow's weight, a thug's blow on her stops it too, and every landed
  blow kicks the lens; see "Contact, reaction and knockout".)
- Hit read: a landed light keeps 2 frames of hit stop (0.033 s); a landed heavy gets 4 (0.067 s, as
  the parry) and a camera punch, the lens 2.5 cm in toward her and back over 0.1 s of real time (a
  half sine), times the camera shake scale (0.3 with reduce camera shake). Every landed strike puts
  `NS_MeleeSpark` on the thug's capsule surface toward her, at the height of whichever of her fists or feet
  is nearest him (clamped to his knees and his face): a hot-gold 26 cm core (blended, so it keeps its colour
  on his white flash) in a 46 cm additive glow, both 0.08 s, and 14 short streaks, scale 1 on a light and 1.4
  on a heavy (times 0.6 to 1 with reduce flashing). When her own body would hide that point from the
  player's lens (her axis within `SparkClearance`, 40 cm, of the line to it) it slides round his body to the
  side the lens sees, up to 0.9 of his radius across. The thug's own 0.1 s body flash is the dominant read;
  the spark only marks where. No damage numbers. (2026-09-29, from the first `hit_spark.png`: the old
  white-violet core was lost in the flash and behind her head, and its sprite outlived its 0.06 s by over a
  second because the template's own lifetime was never replaced; the spark's flashes now set it directly.)
- Ground thud: when a thug's body meets the ground, `MS_Roll_Thump` plays and `NS_KnockdownDust` throws
  dark grey slush up and out from the ground under his pelvis (`GroundThudSound`, `GroundDustVfx` at
  `GroundDustScale` 1 on the thug). A knockdown his clip plays (a heavy, a trip) thuds 1.3 s after he goes
  over (`KnockdownClipThudDelay`: the clip's pelvis is within 35 cm of the ground by then); a ragdoll
  (a blast, a death standing) 0.45 s after (`GroundThudDelay`). Killed mid-fall: no later than 0.45 s more.
  Killed while already on the floor: no second thud. (2026-09-29, from `knockdown_dust.png`: at 0.45 s
  the clip still had him half-way down, and the white `NS_LandingSnow` puff it used did not show on
  snow; `M_Vfx_Smoke` is also faded out within 60 cm of geometry, so the slush uses `M_Vfx_Snow`.)
- Seen in `Hawkeye.Screenshot.Melee` (standalone, 2026-09-29), her own camera: `target_ring.png` 0.1 s into a
  light at a hurt thug (the ring at his feet, his bar), `hit_spark.png` with time stopped in the tick the
  light lands (the gold spark on his chest beside her head, 425 cm from the lens), `knockdown_dust.png`
  0.2 s after a heavy's knockdown thud (the slush round his shoulders). `heavy_strike_2.png` is skipped
  while her set has one heavy.
- Fight camera: two or more alerted, living thugs make a fight when one is within 900 cm (flat) and the
  others within 1500 cm (`FarRadius`, so a gunner hanging back counts), all within 300 cm up or down
  (counted every 0.1 s). With nobody within 900 cm nothing counts. The hip arm lengthens by 70 cm and the lens tips down 4 degrees, blended
  at a constant rate over 0.5 s in and 1 s out, smoothstepped. It is multiplied out by the aim blend
  (the bow's camera is untouched) and by a finisher's push-in (the push reads from the plain hip arm,
  as before; there is no push-in on ordinary strikes, and the fight camera holds through them).
  Indoors the hip arm still never passes `IndoorArmLength`. The spring arm's probe still pulls the
  longer arm in against walls. The log says "fight camera in (2 alerted thugs, one within 900 cm)" and "out".
- Measured (standalone `Hawkeye.Screenshot.Melee`, 2026-09-29): in the open street with two thugs at
  3.2 and 4.4 m the arm is 420 cm (hip 350), the lens 430 cm from the pivot, pitch -4 degrees, Kate's
  head and feet at -0.07 and +0.23 of the screen from its centre, both thugs in frame. With her back
  150 cm from a building the probe holds the lens at 147 cm; she is drawn (not hidden), head at -0.36,
  feet at +0.50 (the bottom edge), both thugs in frame. The shot fails if she is hidden or her head
  or feet leave the screen.
- Clip order for readability (2026-09-29): Kate's heavy is the Roundhouse only. The Surprise Uppercut
  (side on, its hit window shows her back, hips 47 degrees turned, the striking hand 46 cm out to her
  right at 101 cm; the roundhouse's foot is 61 cm out at 83 cm) is `"in_set": false` in the manifest
  until a better second heavy exists; `heavy_strike_2.png` is skipped while the set has one heavy. The
  thug's first fists swing is Jab To Elbow and Standing Melee Punch second (0.3 s into the telegraph
  the jab has his guard up, right hand at 160 cm; the punch is still holding its first frame, hands
  low at 107 to 112 cm, which reads as standing about).

## Arrows in bodies (fixed 2026-09-28, from playtest note "they're like a foot away from her")
- The cause: an arrow meets the capsule (34 to 42 cm radius), not the body, and was embedded at
  the capsule's surface plus 12 cm, then attached there; with the downward angle from a roof the
  capsule's top met it further out still. A mesh without physics bodies left it on the capsule.
- Now an arrow that hits anyone with a skeletal mesh walks its flight line (from 30 cm before the
  capsule impact to 150 cm past it) against the mesh's physics asset shapes at the current bone
  transforms, whatever the mesh's collision, until it enters one. The tip goes 5 cm into that
  surface, the arrow points along its flight, and it attaches to that bone, so it moves with the
  body and falls with a ragdoll. If the line passes through the capsule without touching a body
  (between the legs, beside the waist), it goes into the nearest body to the line at its closest
  point. A mesh with no physics asset takes the nearest bone to the line.
- Damage and headshots use only a real entry: a line that never entered the head is never a
  headshot, even when the nearest body was the head. Shields and walls are unchanged (12 cm).
- Pickups are unchanged: a stuck arrow in a thug or on the ground is recovered within 150 cm; one
  in Kate is not.
- Partner fire (2026-09-28, from Cameron's log: Kate downed by Clint's arrow after its thug died
  mid-flight): an arrow shot by Kate or Clint flies through the other of the pair, whoever is
  player-controlled; a hit on the partner that gets through anyway sticks and does nothing (no
  damage, no stagger, no trick effect). Bola and putty only ever act on thugs. The explosive's blast
  hurts the shooter's partner at half its falloff damage; the shooter still takes her own in full.
- On a character with a body look (Kate as Sparrow, below) the shapes are the look mesh's physics asset and
  the arrow attaches to its bone, so it sits in the body people see; the damage bone is read the same way.

## Kate's body look (built 2026-11-10; setup in claude-docs/animation.md, "Kate's visible mesh")
- `GetMesh()` is the driver: it runs the AnimBP, every montage, the bow and hang IK, root motion and notifies,
  owns the collision and the sockets gameplay measures from, and carries the bow. With a `BodyLook` set and its
  content on disk, the driver is hidden (it still ticks and refreshes its bones unseen) and a child mesh is drawn,
  posed from it each frame; it has no collision. `GetBodyMesh()` returns what is drawn: the look, else the driver.
- Read the body mesh for how she looks (hiding her from a close camera, arrows stuck in her); read the driver
  for everything else. One shadow: the hidden driver casts none.
- The look's hands are the driver's hands (place and turn), so the bow, the string hand and a hang's grip land in
  them; the rest of her keeps the look's own proportions.
- A look whose mesh or AnimBP is not on disk is not applied: one Display line, and the driver is drawn as before.
- Thugs (`AThugCharacter::BodyLook`), the heavy and Clint wear one too (2026-11-11, Quaternius's body, animation.md,
  "Bodies"). A look mesh with no physics asset never simulates: it ticks after physics and follows the driver, whose
  bodies take the hit reaction (`UHitReactionComponent` reacts on the driver unless the look has its own physics
  asset) and the ragdoll. Hit contact always sweeps the driver's bodies. A thug's hit flash and telegraph are on the
  mesh that is drawn.

## Bow aim assist (built 2026-09-28, from playtest note "aim assist for the bows")
- One setting, Aim assist: Off / Normal / Strong (default Normal), a choice row under Controls in
  Settings, saved with the rest (settings version 7; versions 5 and 6 migrate and come up Normal).
- Three parts, all only for the player, only against living, standing thugs she has a clear line
  to, at up to 6000 cm, aimed at their chest (30 cm over the capsule centre):
  - Magnetism: while aiming or drawing, a thug whose chest is within the cone (4 degrees at
    Normal, 7 at Strong) of the view slows the look input toward the centre (to 0.6 of the rate
    at the chest on a pad, 0.85 with a mouse, easing back to 1 at the cone's edge), and pulls the
    view toward his chest at up to 6 degrees per second on a pad (10 at Strong), a third of that
    with a mouse, weaker toward the edge. No pull while the reticle is already over his body
    (within his capsule radius of the line from his feet to his head), so aiming at a head is
    never dragged to the chest.
  - Snap: pressing aim (LT, right click) with a bow turns the view over 0.1 s onto the chest of
    the thug nearest the reticle within 12 degrees. A draw with left click alone does not snap.
  - Bend: on release, if the arrow's line would miss every thug, its launch direction turns up to
    2 degrees (4 at Strong) toward the lead-and-drop aim point on the nearest thug's chest within
    the magnetism cone of the release. A line that already hits someone is left alone.
- Difficulty scales all three: Story 1.25, Normal 1.0, Hard 0.5 of every cone, pull and bend.
  Off turns everything off. AI archers and Clint get none of it.
- Headshots still need real aim: the pull and the bend go to the chest and never act on a line
  already on the body.

## Ranged thug accuracy (built 2026-09-28, from playtest note "they rapid fire arrows and don't miss")
- Tracking lag: an archer or a gunner sees Kate as she was a lag ago (0.4 s Story, 0.32 Normal,
  0.25 Hard): her position then and her velocity then (from 0.15 s of samples before it), taken
  every 0.05 s. He leads from that: her position then plus that velocity over the lag and the
  arrow's flight (with the drop). A steady runner is led right; a turn inside the lag is not seen.
- Accuracy cone: every shot is scattered in a cone of a base (archer 0.75 degrees, gunner 6,
  was a flat 4) plus the angle her sideways speed sweeps across his view in 0.2 s (her velocity
  across his line over the distance, times 0.2, in degrees), plus 3 degrees while she is in the
  air, all times the difficulty's cone scale (1.4 Story, 1.0 Normal, 0.7 Hard). At 8 m, sprinting
  sideways (700 cm/s) is about a 10.8 degree cone at Normal; standing still is the base.
- Erratic movement: a change of more than 90 degrees in her flat velocity (both above 150 cm/s)
  in the last 0.5 s, or a dodge started in the last 0.5 s, makes the shot miss unless she is
  within 600 cm: it goes her capsule radius plus 50 cm wide of her, on the side she came from.
  Each shot of a gunner's burst checks it for itself.
- Archer cadence: his next draw starts no sooner than a random 1.5 to 2.5 s after his last
  release at Normal (3.0 to 4.0 Story, 1.2 to 1.8 Hard), so shot to shot is that plus the draw.
- Every shot is still telegraphed: the archer's draw and glint, the gunner's raised pistol.
- Measured by `Hawkeye.Thug.StrafingKateHitRate` (the thugs' own aim functions and a flown arrow,
  or the pistol's line, against her capsule, 20 shots each): hits out of 20 at Story / Normal / Hard,
  archer at a 700 cm/s sideways sprint at 8 m 3 / 6 / 11, standing at 8 m 20 / 20 / 20, running
  500 cm/s at 20 m 7 / 11 / 11; gunner sprinting at 8 m 1 / 2 / 7, standing 10 / 9 / 14. The test
  holds Normal's sprint to 7 or fewer (about one in three) for both, Story at or under Normal, Hard
  at or over it, and a standing Kate hit at least 15 times by the archer.

## Contact, reaction and knockout (built 2026-11-11, from "it actually makes contact with their body and it ragdolls appropriately")
Rules in `Source/Hawkeye/Combat/HitContactRules.h` (`UHawkeyeHitContactRules`, pure, tested by `Hawkeye.HitContact.*`);
the bodies' side is `UHitReactionComponent` (on Kate, Clint and every thug); the numbers are data: `FHawkeyeStrikeContact`
on every `FHawkeyeMeleeAttack`, the hit-stop, reaction and knockout tuning on the component, `ImpactWeight` on each arrow.
- Contact. Every swing has a target (Kate's assist pick; a thug's or boss's own target, given at his telegraph) and aims at
  a region of him: head (`head`), torso (`spine_03`, else down the spine) or legs (`thigh_r`); a body on the floor is hit
  in the torso. The swing is moved so the striker's capsule stands `ContactDistance` (times the striker's scale) from that
  bone, flat, turned onto it (`PlanContact`): a root-motion clip (Kate) by the motion warp `CombatTarget`, kept on him
  through the wind-up; a procedural swing by her lunge; an in-place clip (thugs) by a slide over the last `ApproachSeconds`
  (0.2) of the telegraph; a boss's lunge stops at the contact distance. The slide is never backward; inside the distance
  only the turn. Past `MaxSlide` or `MaxTurnDegrees` the swing is not moved and whiffs honestly: no warp, no slide, no
  assist forgiveness ("... warp refused: too far ...; it whiffs" in the log). The capsules stop anyone closer than 68 cm.
- The hit. A clip-timed swing (its `ANS_HitWindow`) is found by its limbs: a `StrikeRadius` (15 cm) sphere swept from each
  striking limb's place last tick to now (`Limbs`: fists, feet, both, or a held weapon's hand, middle and far end) against
  the target's physics bodies; his capsule counts only when he has none or lies in a ragdoll. The limb nearest him goes
  first. A procedural swing (no clip) keeps the sweep from the capsule. Kate's forgiveness is unchanged (when in reach).
  Parry, dodge, guard, unblockable, finisher and token rules are untouched: the slide never changes when a blow lands.
- The impact: where it met him (the limb's sweep, or his capsule's front at the striking limb's height), which way it
  travels (the limb's flat motion when over 150 cm/s and not away from him: a hook comes from the side; else the line
  between them), its `Weight` (0 to 1). The struck body hears it before the damage, so a reaction clip picks the side it
  came from (`ClassifyReaction`) and a death throws the ragdoll from there. The melee hit sound plays at the contact point;
  Kate's spark sits at its height.
- (2026-11-12: `Lerp(0.05, 0.09, weight)`, her light 62 ms and heavy 90, never stacking; see "Melee feel".)
- Hit-stop: the world's time at 0.1 (as the parry's stop) for `Lerp(0.02, 0.07, weight)` real seconds, on any landed
  blow Kate or Clint lands or takes (thugs' blows on her stop it too now). It is the world's, not the two bodies' own:
  a per-body stop (tried first) let the AI's, the tokens' and a boss's clocks run on while Kate's stood still, and Santa,
  winding up through her lights, caught her in her frozen recoveries and killed her twice in the grotto lap. Kate's light
  (0.3) is 0.035 s, her heavy (1.0) 0.07 s, as before. It never cuts the finisher's slow motion short. Every landed blow
  of hers kicks the lens in by `Lerp(0.8, 2.5, weight)` cm over 0.1 s of real time.
- Reaction: after the hit-stop, the struck body (Kate's look when she has one) simulates from the pelvis's child above the
  struck body down (spine_01 for the torso, a thigh for a leg) under physical-animation motors (orientation 1200, angular
  velocity 80), a struck body swinging through it excepted, the struck body given `ImpulseSpeed` (420 cm/s) along the blow times `Lerp(0.35, 1, weight)`; its blend
  over the animation rises to 0.8 of that share in 0.04 s and eases back to none by 0.4 s. A guard (incoming damage
  scaled under 1) takes it at 0.35. A body already simulating (a knockdown, a get-up) is left alone. Arrows react too
  (weight `ImpactWeight`, 0.5).
- Knockout: when a thug dies of a strike or an arrow, the ragdoll gets the impact's throw (`ComputeKnockoutImpulse`): the
  struck bone 520 cm/s along the blow, the whole body 200 along it plus 90 up, times `Lerp(0.4, 1, weight)`; an arrow 0.45
  of that along the arrow with no lift and never into the floor; capped at 650 and 300 cm/s. Damping 0.6 / 1.5 while it
  falls. It ignores Pawn (Kate walks through) and now the camera. A clip playing as he dies (the finisher's victim, a
  knockdown, a swing) hands over to the ragdoll over 0.2 s. Once the pelvis is under 10 cm/s for 0.3 s (3 s at most; 0.5
  and 4 before 2026-11-12) the body is put to sleep where it lies. A takedown from behind pushes him over forward at weight
  0.15. A finisher throws through the same rule at `FinisherImpactWeight` (1) along its way (2026-11-12; before, every body
  was set to 900 cm/s on and 450 up and went 8 m). Saves, `ITakedownable`, the mission's kill counts and stealth bodies are
  untouched. Kate's own defeat is unchanged (no ragdoll). Grounded damping and the rest of the 2026-11-12 changes: "Melee
  feel" below.
- Numbers on Normal: Kate's light 66 cm to the head (her chain's third 70 to the torso, weight 0.55), slide cap 290 (the
  assist's 350 less the contact), turns any way; her heavy (the roundhouse) 88 to the torso, weight 1. A thug's fists 72 to
  the head (hands, weight 0.4), bat 100 to the torso (the bat, 0.65), the heavy's bash 85 (hands, a 35 cm sphere, 0.8), his
  slow bat 0.75; slide cap 110, turn cap 75. Bosses use the defaults (80, torso, 120, 75, any limb or what he holds, 0.4).
- Seen (`Hawkeye.Screenshot.Contact`, and in the melee pass): `punch_contact.png`, `hit_reaction.png`,
  `knockout_ragdoll.png`, `knockout_settled.png` (testing.md, "Contact shots").

## Melee feel (built 2026-11-12, from "fighting isn't smooth - punches don't land properly, ragdoll physics are off")
Measured first (`Hawkeye.Audit.MeleeFeel`, docs/playtests/2026-11-11-melee-feel-audit.md). Rules in `UHawkeyeMeleeRules`
(`SelectStrikeTarget`, `IsSlideFloorWalkable`, `ComputeSlideCap`) and `UHawkeyeHitContactRules` (`ComputeHitStopSecondsAfter`,
`ComputeKnockoutDamping`), tested by `Hawkeye.Melee.StrikeTargetChoice`, `.NoMagnetismThroughWalls`, `.NoMagnetismOverLedges`,
`.LightBufferedAfterHeavy`, `Hawkeye.HitContact.HitStopScaling`, `.KnockoutGroundedDamping`. Numbers are data:
`FHawkeyeStrikeMagnetism` on each melee component, `FHawkeyeHitStopTuning` and `FHawkeyeKnockoutTuning` on each hit reaction
component, Kate's `MeleeAssistRange` / `MeleeAssistAngleDegrees` / `MeleeAssistNearRange` / `LightBufferSeconds`.
- Who a strike goes at: the best (as "Melee assist") inside 60 degrees of the stick when it was pushed in the last 0.15 s;
  nobody there, inside 60 degrees of the camera's forward; nobody in either, the nearest within 150 cm whatever the angle.
  Within 350 cm flat and 200 up or down, as before. A thug behind a wall, across a gap or below a ledge is never a pick.
- The pull (warp, lunge or slide) is refused, and the swing turns and whiffs, when anything solid is on the chest-to-chest
  sight line (an 8 cm sweep on Visibility; bodies do not count), or the floor from her feet to where the swing would stand
  her (sampled every 25 cm) drops more than 60 cm or rises more than 45 (`"a wall between"`, `"no floor on the way"` in the
  log). Then not even the attack's own step is taken. The assist's forgiveness never lands through a wall. Thugs' slides
  obey the same rule. In the air (no floor under her) the floor rule passes.
- Speed cap: the pull closes no faster than `MaxClosingSpeed`, averaged over the swing's wind-up (its clip's hit time) for a
  warp or a lunge, over `ApproachSeconds` for an in-place slide: Kate 1,100 cm/s (her clip lights, 0.24 s: 264 cm, so
  the whole assist range still lands), a thug 550 (110 cm over his 0.2 s slide). Past it, `"too far"`.
- A blow moves the region it was aimed at (the head for a jab, the ribs for the roundhouse, the torso for a body on the
  floor), not the body nearest the fist (it was his own guarding hand).
- The struck body's kick is set on the body itself (`FBodyInstance::SetLinearVelocity`): `AddImpulse` refuses a query-only
  mesh, every thug's driver, and threw every thug's kick away until now.
- A reaction never simulates a body with no body above it (a component follows its root body): on Kate's Sparrow, which has
  no pelvis body, that carried her whole look off her capsule a metre per punch she took. Such a body stays on the
  animation and the bodies under it take the kick. A ragdoll owns the body only when its pelvis simulates.
- Kate's and Clint's physical reaction is off (`bPhysicalReaction` false on their hit reaction component): once it ran on
  Kate's look it crashed the game now and then inside the engine's physics blend on that secondary mesh ("Array index out of
  bounds: 29 from an array of size 0", one lap in thirteen and three audit runs), and on her driver it once stalled the game.
  Their hit clip for the side the blow came from, the lean and the hit-stop read every blow; the thugs' drivers react as
  above. Before this pass her reaction never ran past its first tick anyway (it read her own chain as a ragdoll).
- Hit-stop: `Lerp(0.05, 0.09, weight)` real seconds (her light 62 ms, the heavy 90). A blow landing within 0.15 s (real) of
  the last stop's start gets none: stops never stack.
- Knockout: once he is down (0.4 s after the knockout at the earliest, the pelvis within 40 cm of the floor) the damping
  rises from 0.6 / 1.5 to 4 / 8 over 0.5 s: he lands with weight and stops instead of rolling on. A body thrown off a roof
  falls at full speed until it is down.
- Buffering: a light pressed in the last 0.3 s of any recovery (a heavy's, a light whose chain is over) goes the moment that
  swing ends, starting a fresh chain. Inside a chain's window it is kept as before.
- Not changed: the light starts on the button's release (a 0.4 s hold is the heavy), so a tap's own length comes before the
  swing; the audit leaves that to Cameron.

## Attack tokens (built 2026-11-10; group pressure pass the same day)
Rules in `Source/Hawkeye/Combat/CombatTokenRules.h` (pure, tested by `Hawkeye.Tokens.*`); the state is
`UCombatTokenSubsystem`, one per world. Before tokens every thug swung and fired on his own clock, so four of them
hit her at once from off screen. The first token pass then made them take polite turns (one swing at a time below
Hard) and fights got easy: the chapter 2 crew of five went down in 15 s without a hit on the scripted player. The
group pass lets several come at once, Spider-Man style, and keeps every blow answerable.
- Each target (Kate, or Clint when a thug is fighting him) has two budgets, melee (fists, bat, the heavy's
  bash and swing) and ranged (a gunner's burst, an archer's draw): how many of her attackers may be
  attacking at once. Melee 1 / 2 / 3 and ranged 1 / 1 / 2 at Story / Normal / Hard, then scaled to the group:
  at most `TokenGroupShare` (0.5 on all three) of the attackers on that budget, rounded up, never under 1
  (`ScaleBudgetForGroup`). The attackers are everyone who asked on it in the last 3 s and is not dead
  (`RosterSeconds`), in line or not, so one staggered thug does not shrink the group. Two thugs share one token, three or four share two, Hard's three needs five or six.
  Story stays one swing and one shot.
- Two at once come from different sides: a second (or third) melee token goes only to a waiter standing at least
  `MeleeSeparationDegrees` (90 / 60 / 45) round her from every melee holder (`IsSeparatedFromHolders`); the first in
  line who qualifies is granted, the rest wait.
- Blows land in a rhythm: every attack books when it will land (a swing's wind-up end, a burst's first shot, a draw's
  release) and is pushed later, never earlier, until it is `ImpactIntervalSeconds` (0.6 / 0.45 / 0.35) clear of
  every other blow booked on her, melee and ranged alike (`StaggerImpactDelay`, `UCombatTokenSubsystem::ScheduleImpact`).
  So two swings, or a swing and a shot, are answered by two parries in time or one dodge out of reach; never both
  at once. A broken or finished attack frees its booking.
- After a hit from someone else lands on Kate nothing else from anyone else does for `HitGraceSeconds`
  (0.8 / 0.6 / 0.45; `UHealthComponent::HitGraceSeconds`, 0 for everyone but her), longer than her 0.35 s stagger,
  so no stunlock. Her own boomerang and a fall neither start it nor are held off.
- A token is held from its grant until the attack is over: a melee thug's step in, telegraph, swing and
  recovery; a gunner's telegraph and burst; an archer's draw (a hold included) until he looses. After a
  token is given back, that budget grants nothing for a gap of 0.9 / 0.45 / 0.3 s.
- Who gets it when several ask, in order: anyone who has waited 4 s (oldest first, so nobody starves);
  then anyone but the one who attacked last on that budget (he goes to the back); then whoever is on
  screen (within 50 degrees of where she looks: her camera, or the eyes of an AI target) before anyone
  off it; then the nearest (in 50 cm steps); then whoever has waited longest. An off-screen thug is granted
  only when nobody on screen is asking, he is overdue, or he is the one standing on the other side, and his
  telegraph then lasts at least 1.2 / 1.0 / 0.8 s (a swing's wind-up, a burst's raised pistol, a draw).
- The off-screen warning: a telegraph whose glyph over his head would be off the screen or behind the lens is drawn
  round Kate instead, on an ellipse 0.16 of the screen's width and 0.24 of its height round her chest, on his side
  as seen from above (ahead at the top, behind at the bottom), 1.2 times the telegraph glyph, the same red "!" or
  yellow "!!" (`HawkeyeCombatReadability::IsOffScreen`, `ComputeOffScreenCuePosition`, drawn by the thug overhead
  widget). It is the game's spider-sense.
- Only an attacker ready to attack is granted (a melee thug within reach of his place on the ring, off his
  cooldown and not backing off; a gunner off his pause and not jammed; an archer past his gap with a line).
- A melee thug without a token closes to a ring 300 cm round her and holds his place on it facing her;
  places are spread at least 70 degrees apart (40 before the group pass; wider than Normal's 60 so neighbours
  can both come) in the order the thugs stand round her, each moving as little as he can. He is not parked there: every 1.0 to 2.2 s he either shifts 25 degrees to one side of his
  place (the other side next time) or, a 0.35 chance, feints 120 cm in toward her and back out over 0.9 s
  (`FHawkeyeRingFidget`, `AThugAIController::RingFidget`, data on the thug). A feint is no attack: no glyph, no
  hit. A boss does not fidget (`bFidgetOnRing`, off by the boss fight): Tomas's hazard lure counts on where he waits. Granted, he steps in and swings; if he has not started his swing 1.5 s after the grant he gives it
  back. A gunner or archer without one holds his aim and waits.
- Ranged pressure is what was there: a gunner's raised pistol (and the glyph inside 600 cm) or an archer's glint
  says a shot is coming, a standing Kate is hit, a dodge or a turn makes it miss ("Ranged thug accuracy"), and the
  impact booking keeps its first shot clear of any swing.
- Tokens never leak: one is given back when the attack ends or is broken, and taken away on a stagger,
  a knockdown, putty or a bola, death, losing the target, leaving the attack, her going down, a
  challenge's calm, thinking off, or a new target. The budget holds attackers by weak reference, so a
  destroyed one simply goes; a holder who stops asking for 0.75 s, or has held one for 6 s, loses it.
- `GetPressure` / `ResetPressure` count, per target, the most holders at once by kind and together, grants,
  grants that overlapped another melee holder and blows pushed later; the laps record their own per-fight numbers
  (testing.md, "Fight pressure").
- Unchanged: the back-off after two swings, the cooldowns, the stand-off round a downed player (the
  same ring walk, 450 cm, orbiting), difficulty damage, squad alerts.
- `hawkeye.DebugTokens 1` draws a red line from each holder to his target and a grey one from each
  waiter, with his place in the queue over his head, and logs every grant and return at Log (Verbose
  otherwise).
- Measured (standalone, Normal, 2026-11-10, seconds / hits taken, main just before then after): roof 4.4 / 0
  then 4.3 / 0; archer duel 8.9 / 0 then 8.0 / 0; street 16.6 / 0 then 16.7 / 0 (0 untelegraphed); mugging
  8.0 / 0 then 8.1 to 13.6 / 0 over three runs; ambush 14.6 / 2 then 12.4 to 16.1 / 0 to 2; the loud interior
  lap 32.4 / 4 (84 damage) then 39.9 / 4 (66), its hall fight 18.9 then 27.3 s. All won. Headless, four fists
  thugs round a still Kate for 30 s: at most one swinging at once on Normal (two on Hard), every one swinging 3
  or 4 times, every swing from behind her winding up for 1.0 s. `token_ring.png` (`Hawkeye.Screenshot.Melee`):
  four who came at her from 7 m, after 5 s two in front 233 cm out and two behind 366 cm out, all standing,
  each facing her.
- Measured after the group pass (standalone, Normal, 2026-11-11, the laps' own fight records; before is the same lap
  on main the day before): chapter 2's crew of five 15.0 s / 0 hits / 1 dodge / 2 parries (bot counts) before, 9.8 to
  14.5 s / 0 to 1 hits / 0 to 2 dodges / 4 to 5 parries after, two melee holders at once (0.9 s of it); the four
  seen elves two at once for 1.6 s, 3 hits; Tomas's crew two at once for 1.5 s, 1 hit, 7 parries; Santa 25.2 s before,
  41 to 43 s after (50 s while bosses still fidgeted); Tomas's phase 87 s before, 105 s after, 12 winded windows (10).
  Headless, four fists thugs round a still Kate for 30 s: two swinging at once on Normal and Hard, never closer than
  90 degrees nor 0.45 / 0.35 s apart at impact, 6 to 10 swings each (3 or 4 before). The bot still clears every fight;
  the ones with two melee thugs or fewer (the mugging, robbery, rooftop, the street pair and heavy) cost it nothing, by
  the group rule.

## Side challenges (built 2026-09-27; names are placeholders)
- `AChallengeStart` pedestals (purple-lit, E to start) and one `UChallengeDefinition` per
  challenge under `/Game/Challenges`, planned by `create_challenges.py` and placed by the
  generator. Three archery ranges on rooftops 133 to 178 m from the start; three traversal
  routes from street corners (the third is 331 m out because routes needing a mantle are
  rare).
- Archery: 12 ringed glowing targets 12 to 40 m out on other roofs and fire-escape
  landings with clear lines, 3 or 4 moving on a 6 m track. Score 10 / 5 / 2 at radii 8 /
  18 / 30 cm, 60 s, medals at 36 / 60 / 84, reward an arrow refill.
- Traversal: 8 floating 200 cm checkpoint rings across roofs using grapple, mantle, and a
  fire-escape descent; 150 s limit; gold under 60 s, silver under 90, bronze under 120.
  Every planned grapple leg passes the grapple's own clearance check.
- A run fails on time-out, on leaving a 100 m radius, or if the player goes down. Thugs
  within 60 m go Calm for the run. Best score or time and medal per challenge are saved.
  HUD panel during a run; results card with Retry and Leave; next checkpoint or remaining
  targets use secondary objective markers.
- Scripted: archery clears 12 of 12 in about 17 s (bronze by score); traversal in 18 to
  19 s (gold). Distant targets are small at 30 to 40 m. The pedestal light was too strong
  and is being dimmed.
- Finding them (2026-09-28, playtest note 13): the compass and world markers above; the
  pause menu's "Mark nearest challenge" puts a marker on the nearest pedestal (cleared on
  reaching it or starting any challenge); the first time the player comes within 40 m of a
  pedestal a "[Challenge nearby]" toast names it (once a campaign, ids kept in the save as
  `NoticedPlaces`). The cap and ring icon are a deep purple (0.5, 0.06, 1) at 0.35 / 0.6
  night emissive, since 2.5 clipped to white under the night's +2 EV; by day the glows take
  the time of day's GlowScale, 3 (checked in challenge_pedestal.png at night and day_pedestal.png).

## Difficulty and accessibility (built 2026-09-27)
- One difficulty setting, Story / Normal / Hard (default Normal), asked once at New Game
  and changeable in Settings; `UDifficultySubsystem::GetScalar` holds the whole table.
  Thug melee and gunner damage 0.6 / 1.0 / 1.4; thug health 0.8 / 1.0 / 1.2; archer draw
  1.5 / 1.2 / 1.0 s; parry window +0.15 / 0 / -0.1 s (Story holds an early tap up to
  0.15 s; Hard ignores the first 0.1 s of a telegraph); regen delay 3 / 5 / 8 s at
  10 HP/s (regen didn't exist before and applies to Kate only); fall damage 0.5 / 1.0 /
  1.0; Story adds 2 to each trick arrow cap (never the grapple, which has no count). Added 2026-09-28 ("Ranged thug accuracy", "Bow aim
  assist"): ranged tracking lag 0.4 / 0.32 / 0.25 s; ranged cone scale 1.4 / 1.0 / 0.7; archer
  gap before the next draw 3.0-4.0 / 1.5-2.5 / 1.2-1.8 s; aim assist scale 1.25 / 1.0 / 0.5. Added
  2026-11-10 ("Attack tokens", the group pass): melee tokens 1 / 2 / 3; ranged tokens 1 / 1 / 2; group share
  0.5 / 0.5 / 0.5; gap between tokens 0.9 / 0.45 / 0.3 s; an off-screen attacker's shortest telegraph 1.2 / 1.0 /
  0.8 s; two melee attackers at once at least 90 / 60 / 45 degrees apart; blows at least 0.6 / 0.45 / 0.35 s apart;
  Kate's post-hit grace 0.8 / 0.6 / 0.45 s.
  `-Difficulty=Story|Normal|Hard` on the
  command line overrides without saving. Every scripted fight is won on all three.
- Accessibility, all persisted (settings version 5; 6 adds the time of day; 7 the aim assist; 8 the
  sprint mode; 9 the music volume; 10 the hints toggle; 5 to 9 migrate with the new fields at their defaults): subtitle size
  20 / 26 / 34 px with a
  background opacity slider; hold or toggle for aim (default hold) and crouch (default
  toggle); a 1 px outline on the alert glyphs; four colour palettes (default,
  deuteranopia, protanopia, tritanopia) for the reticle, markers, hotbar, glyphs, and
  health bars; reduce camera shake to 30%; reduce flashing to 30% for the EMP split and
  the explosion light (the parry ring shrinks to 30% instead, it has no brightness
  input); Replay flashbacks in the pause menu, slides only, seen-list stored with settings.
- HUD scale 0.8 to 1.4 applies to HUD text, hotbar, markers, and compass.
- Challenge pedestal light 135 lm over 400 cm; the cap and icon read white rather than
  purple until the 2026-09-28 fix in "Side challenges".

## Street crimes (built 2026-09-27; names are placeholders; rules also in Source/Hawkeye/Crime/CrimeRules.h)
- `City_CrimeSpot_` actors: 8 street corners (each with a robbery escape point 58 to
  62 m along the sidewalk), 4 rooftops with anchors, and 2 to 4 alleys (2026-09-29); none
  within 40 m of a safehouse, a pedestal or the interior entrance's doorstep, all at least
  35 m apart.
- Alleys (2026-09-29): `create_crimes.py` casts a ray off every wall of every footprint within
  250 m of the PlayerStart to the next building; a 3 to 6 m gap whose middle is 8 m past every
  carriageway's edge, 1.4 m from every wall, off the park and clear of street clutter is a
  candidate, and it must join a sidewalk by a walk never nearer than 1.4 m to a wall, at most
  50 m on a 50 cm grid. On the East Village block: 461 rays found gaps in 71 passages; 297 fell
  in a keep-out or within 35 m of a spot; of 30 passages tried, 22 were dead ends (rear yards
  with no way out but through a building) and 5 too deep; 3 were placed (`City_CrimeSpot_12` to
  `_14`, 5.8, 4.5 and 5.4 m wide by footprint, 5.5, 4.0 and 5.0 m by the level's walls). Alley
  spots face along the passage (`ACrimeSpot::bAlley`) and take the mugging and the ambush; an
  ambush there lines its four up along the passage (pairs 5 and 3 m either side, 60 cm across,
  `UCrimeRules::AlleyRosterOffset`) instead of on a 5 m ring in the walls. A block with fewer
  than two alleys would get two spots on the park's interior paths instead. `verify_city.py`
  checks the count, the crimes, the width by traces and a navmesh path to the nearest corner
  spot.
- A crime is due every 90 to 150 s of roaming (not in combat, a challenge, or a chapter
  beat), at a spot 25 to 40 m away and 60 m from the last, preferring spots out of sight.
  One at a time. `hawkeye.CrimeInterval` and `hawkeye.CrimeType` cvars control it;
  automation never gets a crime unless the interval is set.
- Mugging: fists and bat thugs around a grey `BP_Civilian`; the victim is hit at 15, 30,
  and 45 s while an unalerted thug is beside him, and the third hit fails it. Robbery:
  a runner (380 cm/s) who leaves when Kate is within 20 m, a thug is alerted, or 40 s
  pass, and drops the loot when downed; picking up the loot ends it. Ambush: four thugs
  alerted from the start. Rooftop: two thugs and an archer.
- Leash (2026-11-10, after `Hawkeye.Lap.CrimeRooftop` failed once in two batches: the archer's zip, which takes any
  anchor 15 to 25 m from her, put him on another roof, and the lap walked off the edge after him or stood aiming at
  him until it timed out). A crime with a `LeashRadius` (`UCrimeDefinition`, cm; the rooftop's is 25 m) leashes its
  roster to the spot (`AThugAIController::SetLeash`): a relocation never lands more than that from the spot, and on a
  roof spot only on that roof (the building a trace down from the spot hits). With no anchor inside the leash he
  stays and steps back on his roof. `Hawkeye.Archer.LeashKeepsHimOnHisRoof`; the lap asserts no crime thug stood off
  the crime's roof (5 runs in a row green, 9.4 to 13.5 s, 0 or 1 hit; the archer relocated to an anchor on his own
  roof in four).
- Stopping a crime gives +5 standard arrows (never grapple arrows, which have no count), a toast,
  and a per-type count in the save.
  Crime thugs despawn 60 s after success once the player is 40 m away, 20 s after failure.
- Scripted laps (standalone `-game`, `Hawkeye.Lap.Crime*`, 2026-09-29), each started by the
  subsystem's own rules through `hawkeye.CrimeType`, each paying +5 arrows, the toast
  ("[Crime stopped] / [Crime: <type>]   +5 arrows"), the count and the campaign save:
  mugging at the corner `City_CrimeSpot_0`, 11.8 s, 0 hits, the victim unhurt; robbery at
  `_0`, 6.4 s, 0 hits, the runner tripped by a bola 8 m from her 63 m short of his escape, the
  loot walked over; ambush in the alley `_12`, 13.5 s, 0 hits, a bola on the gunner then
  strikes, a parry and three finishers; rooftop on `_10` (13.7 m), grappled up from the street
  27 m out in one press every run, 9.2 to 15.7 s over four runs with 0 or 1 hit (the archer, 30),
  two or three arrows, six to nine strikes and a finisher. A crime waits out
  a fight: with a thug still alerted or Clint's fight clock running (10 s after the last blow) the
  schedule does not run, so back-to-back laps start 5 to 10 s after set-up. The laps found: a thug
  walking to where she landed a zip by a parapet asked for a goal off the roof's navmesh and failed
  (fixed: `AThugAIController::RequestMoveToLocation` walks to the goal's nearest navmesh point within
  2 m); and their own slot change on the release frame loosed a standard arrow instead of the bola.

## Chases (built 2026-11-10 for Act 1's "Night Shift"; rules also in Source/Hawkeye/Chase/ChaseRules.h)
- A chase is data: a `UChaseRouteDefinition` (`DA_Chase_<chapter>`, planned from the district by
  `Tools/Editor/create_chase.py`) of ordered thief waypoints (feet points, cm), each with the leg that
  reaches it from the one before: Run, Vault, Climb, JumpGap or Drop. A chapter names its chase on
  `UMissionDefinition::Chase`; `UChaseSubsystem` runs it. Another chapter's chase is another asset.
- It starts at the chapter's opening (the frame its title card goes up: no standing start) or when a save
  made during it loads. At the opening Kate is put on the route's start, facing along it, moving at her
  sprint speed, and the thief (the route's `ThiefClass`, thinking off, tagged `ChaseThief`, never saved by
  SPUD) stands `StartLead` (1100 cm) along the route ahead of her.
- Progress: the thief's is his distance along the route. Kate's is the route distance of the point of the
  route nearest her, looked for from `BackWindow` (1500 cm) behind her last progress to `AheadWindow` (6000)
  ahead of it, and only among points within `OffRouteDistance` (2000 cm, in 3D) of her; with none that close
  her progress holds. It never goes back, and never on by more than 1.5 times what she really moved that frame
  plus 300 cm a second, so a projection that jumps to another part of the route is followed, not leapt to. The
  lead is the thief's progress less hers, but never less than 0; when she is off the route it is the
  straight-line distance between them.
- Rubber band: on Run legs the thief moves at `BaseSpeed` (620 cm/s, under her 700 sprint) times a factor:
  `CloseBoost` (1.15: 713 cm/s, so on foot she cannot close on him; her grapples can) while the lead is under
  `CloseLead` (700 cm), 1.0 up to `FarLead` (2500), then down in a
  straight line to `MinSpeedScale` (0.8) at `LoseLead` (4500) and held there. His speed moves toward that at
  most `SpeedChangeRate` (400 cm/s per s). He never stops before the route's end, never waits and is never
  placed, except once after the dumpster beat (below). Vault 0.5 s, Climb 0.8 s, JumpGap 0.45 s plus 0.06 s per
  metre (at most 1.4 s), Drop the height at 900 cm/s plus 0.3 s: the other legs take their own time whatever
  the lead.
- Lost: the lose clock runs while the lead is at least `LoseLead` and is back to 0 the moment it is under.
  At `LoseSeconds` (6 s) the chase is lost: "[He got away]" on the HUD, the thief goes, and the last
  checkpoint loads (`AHawkeyeGameMode::ReloadLastCheckpoint`, 1.5 s fade). The clock is held during the beat
  and while she is down.
- Objectives and lines by place: a waypoint may name an objective (completed when Kate's progress reaches
  that waypoint's distance less `ReachTolerance`, 300 cm) and a dialogue sequence (played then). Moments the
  route names sequences for: the first vault she makes, the first time the lead goes past `FarLead`, the
  botch, the dumpster, the climb out, the thief cornered, the catch. Each plays at most once a campaign (the
  dialogue rule), so a restart does not repeat them. While a chase runs the thief's position is registered
  as the marker point of the route's `ThiefObjectives`.
- Caught: the thief is caught when he is taken down or downed by anything (a bola, the putty, health 0), but
  only once he is cornered at the route's end: running, he refuses a takedown
  (`AThugCharacter::bRefusesTakedown`) and a hold or knockdown does not stop the chase, so the chapter's
  story (the beat, the corner and its lines) is never skipped by catching him early. A catch completes every open objective the route names, in order, after its Caught sequence has spoken (at
  most `CaughtHoldSeconds`, 4 s), so the chapter ends on the line and then the end card. At the route's end
  he is cornered: he stops and turns his back on her (facing straight away from her every frame), so the
  ordinary takedown from behind works from wherever she arrives. He is never alerted: the chase keeps him
  calm, so he never fights, never counts as a fight for the autosave, the music or the dialogue.
- Restore: a save during a chase records the route, the thief's progress, the beat's state and Kate's
  progress; a load puts the thief at the smaller of his saved progress and Kate's plus `RestoreMaxLead`
  (2200 cm), with the lose clock at 0, so a checkpoint never reloads into a lost chase. Saved under a lead of
  `RestoreMaxLead` or less, Continue brings him back exactly where he was.
- The beat (one per route, optional): a zone (a box on the near roof), an anchor across, a dumpster on the
  ground, the points Kate falls to and climbs out to, and `ResumeLead` (1600 cm). It starts only when Kate
  starts a zip from inside the zone at an anchor within 300 cm of the beat's anchor, and only once. 0.3 s
  into the zip the line lets go: she falls into the dumpster (0.7 s, a burst of wrapping paper where she
  lands), sits in it 1.2 s and climbs out over its rim onto the exit point (0.9 s), walking: 3.1 s in all,
  her look input hers throughout, move input ignored. As the line lets go the camera is turned to look
  along the alley (the way with more room behind her) and down 25 degrees, and the dumpster does not push
  the camera; a warm light hangs over it (the route spawns it with the dumpster). Then the thief is put `ResumeLead` ahead of her
  progress (the beat's waypoint) and the lose clock reset. On a replay (its botch line already played this
  campaign) a jump press 0.3 s or more into it skips to the exit point. If her progress passes the beat's
  waypoint any other way (she fell, jumped, grappled elsewhere) the beat is marked passed and never plays;
  if it runs longer than 5 s (something interrupted a move) she is put on the exit point, walking, move input
  back, and the chase goes on.
- Partner (2026-11-10): `UMissionDefinition::bPartnerPresent` (on by default). With it off the game mode
  benches every partner when the chapter starts (and a partner controller's first possession in such a chapter
  benches him; a later one, a switch, is left alone): his controller stops thinking, his pawn is hidden with its
  collision off, and the HUD's partner line, the switch, the partner mark, banter, revives and the fight check all
  treat him as absent (`GetPartner()` is null). A chapter with him brings him back. `bAllowSwitching` (off by
  default) still gates the switch on its own.
- World furniture of other chapters: an objective volume, a thug group or a chapter-end interactable whose
  objective the running chapter does not have does nothing and logs nothing above Verbose; actors tagged
  `objective:<id>` (the beacons) are hidden, their lights off, while the running chapter has no such objective.

## Safehouses and fast travel (built 2026-09-27; rules also in Source/Hawkeye/World/SafehouseSubsystem.h)
- Two safehouses: "[Safehouse 1]" at 140 East 7th Street (OSM W248142338) and
  "[Safehouse 2]" on Avenue B at East 10th (OSM W250264779), 278 m apart. The generator
  picks the second as the nearest qualifying storefront at least 250 m from the first,
  skipping buildings whose fire escape would cut through the sign. Both must be entered
  once to be discovered; undiscovered ones show greyed as "[Undiscovered]". Refill arrows tops up
  standard and trick arrows only; the grapple has no count to refill.
- Fast travel from a safehouse menu to any other discovered one: fade out 0.5 s, hold
  0.35 s during which Kate and Clint are moved to the destination door and the game
  autosaves with them there (destination recorded as last used), fade in 0.6 s; about
  1.5 s fade to fade, no map reload. Refused with a "[Can't fast travel now]" toast during
  a crime or challenge, a fight (a thug alerted, or Clint's fight clock running) or while she is
  down (2026-09-29, `Hawkeye.Lap.Campaign`: the ArcherPair downed her during the 1.5 s fade and the
  travel's autosave was refused).
- Both safehouses always show on the compass, found ones as filled houses, unfound ones
  hollow. The pause menu's "Mark nearest safehouse" marks the nearest one whether found
  or not (it only looked at found ones, so with none found it did nothing but toast "[No
  safehouse found yet]": playtest note 12). The first time the player is within 60 m of an
  unfound one, a "[Safehouse nearby]" toast and the marker on it (once a campaign, in
  `NoticedPlaces`). Chapter select is still a stub. Crime spots keep 40 m clear of both.

## World map (built 2026-09-29, from "couldn't find the safehouses or the pedestals")
- M on keys, a 0.4 s hold of D-pad up on a pad (View is already the inventory on a tap and the
  quiver wheel on a hold, and R3 the partner mark, so the map takes the D-pad hold the phone
  already uses on down; a tap of up is still the standard arrows, now on release). Refused, like
  the phone, under any menu, the inventory, the phone, a flashback, a close-up or a chapter beat.
  The game pauses under it. M, Escape, B, Menu or D-pad up close it; Escape never also opens the
  pause menu. No minimap: the compass is the HUD.
- `UHawkeyeMapWidget`, painted in `NativePaint` with the engine's Slate draw calls (`MakeCustomVerts`
  for filled footprints, ear-clipped once by GeometryCore; `MakeLines` for streets; `MakeBox` and
  `MakeText`); no plugin. It draws `DA_EastVillage_Map` (`UCityMapData`, written by
  generate_city.py): footprints dark grey, the park a muted green, streets lighter grey at their
  carriageway width, on a near-black ground. North up (world -Y), the district's bounding box fitted
  into the screen less a 16 px gutter and the legend row.
- Icons (`FHawkeyeMapIcons::Build`, drawn in this order): the interior door, other secondary markers
  (a small purple diamond), safehouses (the compass's house, filled once found, hollow before,
  "[Unknown safehouse]" until found), pedestals (the medal, gold, silver or bronze for the best medal
  earned, the palette's purple before one), the crime in progress (a danger-red diamond with "!"),
  the current objective (the cream diamond), Clint (a small grey arrow) and the player (a purple
  arrow along her heading) on top. A marked place gets a cream ring; its own secondary marker is
  not drawn again. Icons take the HUD scale and the colour palette. A legend row runs along the
  bottom with each glyph and the key hints (keyboard or pad, whichever was touched last).
- A cursor (the mouse, or the left stick at 700 px/s, pushing the map when it meets the edge) over
  an icon (within 16 px) shows its name, medal and distance from the player; markable icons win
  over nearer ones. Enter, Space, A or a click on a safehouse or pedestal marks it through the
  "Mark nearest" plumbing (`USafehouseSubsystem::MarkSafehouse`, `UChallengeSubsystem::MarkChallenge`),
  so the world marker and the compass follow; marking one kind takes the other kind's marker down
  (one marked place from the map), and marking the marked place again clears it. Fast travel is
  not on the map; it stays in the safehouse menu.
- The wheel (x1.25 a notch) or the right stick zooms 1x to 3x about the cursor; a drag pans. The view
  never leaves the district; at 1x it is centred and does not pan. The map opens at 1x with the
  cursor on the player. (`Hawkeye.Map.*`, `map_open.png` in the Kate pass.)

## Combat animation clips (built 2026-09-27/28; pipeline in claude-docs/animation.md)
- A character's clips come from a `UCombatAnimSet` data asset (`DA_AnimSet_Kate`, `_Clint`,
  `_Thug`, `_Archer` under `/Game/Blueprints/Animation`), one soft montage per role: Light1,
  Light2, Light3, Heavy, Kick, Parry, DodgeForward/Back/Left/Right, HitFront/Back/Left/Right,
  Knockdown, GetUp, FinisherAttacker, FinisherVictim, FinisherBow, BowDraw, BowAimIdle,
  BowFire, BowNock. `CombatAnimSet` on `AHawkeyeCharacter` and `AThugCharacter` picks it.
- **The fallback rule.** A role whose montage is empty, fails to load, or will not play (no
  anim instance, wrong skeleton) does exactly what the game did before clips existed: the
  strike pose and lunge, the procedural dash, the hit lean, the ragdoll knockdown and its
  0.4 s blend up, the IK bow. Every rule in the melee, dodge, finisher and bow sections holds
  either way; a clip changes how it looks and, for strikes only, when the hit lands.
- Only Light3 has a data fallback: with no Light3 clip it uses Kick.
- Variants (2026-09-28): a role can hold several clips. The first in `Tools/Data/Anims/manifest.json`
  order that exists for that character is the role's slot (variant 0); the rest go in the set's
  `MoreVariants` for that role, in manifest order (`create_combat_anims.py` writes both; a clip marked
  `"in_set": false` is imported but left out). Each strike (the melee component's swings: Light1-3,
  Heavy, Kick) takes the next variant of its role in order, round again after the last, so with two or
  more no clip plays twice running; a role with one clip plays it every time, as before. The cycle is
  per character and per role (a chain's Light1 and the next chain's Light1 differ). The log says which:
  "... variant 2 of 2 for ECombatAnimRole::Light1: AM_Light1_Punching". Every other role (dodges,
  reactions, knockdown, finisher, bow) plays its first clip only. A variant that does not load gives way
  to the role's first clip.
- Clips are retargeted, never used on their source skeleton: Kate and Clint's onto the UEFN
  mannequin, thugs' and archers' onto the UE4 mannequin they wear. Additive source clips (Paragon's
  hit reactions) are baked to full poses. Standing clips must keep the pelvis within 35 cm of the
  reference height with the root held where the game holds it (locked, or extracted as root motion);
  the import reports any that do not, and the self-test fails past 15 cm. The root track carries only
  horizontal travel on the ground, never the pelvis's height (2026-09-28: Mixamo clips had the hips'
  90 cm on the root, and everyone playing one knelt).
- Strikes: Kate's light chain is Light1, Light2, Light3; her hold is Heavy; a thug's fists
  are Light1, his bat and the heavy's bash and slow swing are Heavy. `FHawkeyeMeleeAttack.AnimRole`
  names the role. A montage whose slot is `UpperBody` plays on the post-process instance
  over locomotion; any other slot plays full body (`DefaultSlot` on the sample's AnimBP for
  Kate and Clint, the post-process graph's `DefaultSlot` for thugs, whose main instance is a
  single clip).
- Hit timing with a clip: the sweep runs when the montage's `ANS_HitWindow` begins, and
  again each tick until it finds someone or the window ends; if nobody was found by the end,
  the swing missed. A montage with no `ANS_HitWindow` keeps the attack's WindupSeconds and
  RecoverSeconds timers while it plays.
- Chain timing with a clip: after a light lands, or misses (its `ANS_HitWindow` ended on nobody),
  the chain stays open (the 0.35 s timer does not run) until the montage's `ANS_ComboWindow` ends; while that window is open the next
  strike starts at once, cutting the current montage. A press between the hit and the window,
  or while the hit window is still open on nobody, is held and goes when the window opens. A montage with no `ANS_ComboWindow` keeps the
  0.35 s timer and ends the swing at RecoverSeconds.
- Kate's strike timing with clips (2026-09-28; each clip's start, end, rate and windows are in the
  manifest, in the source clip's seconds): Light1, Light2 and Light3 land 0.24 to 0.26 s after the
  input with either variant, Kick 0.29 s (Kicking and Side Kick), Heavy 0.39 s (the Roundhouse, her only
  heavy since 2026-09-29; the Surprise Uppercut, 0.41 s, is imported but out of the set). A light's montage lasts 0.54 to 0.81 s; its combo window opens
  about 0.1 s after its hit and closes 0.3 to 0.5 s after it, so the 0.35 s chain rule holds to within
  about 0.15 s either way. Pressed as each lands, three lights land at about 0.25, 0.6 and 1.0 s and the
  chain is over by about 1.4 s. The procedural lights still land at 0.1 s.
- Thugs keep their telegraphs: the telegraph decides when the hit lands, the clip only how it looks.
  A thug's strike montage opens its `ANS_HitWindow` exactly at the attack's WindupSeconds (0.6 s
  fists and bat, 0.8 s bash, 1.0 s slow swing), so parry and dodge windows are unchanged. It plays
  at a rate between 0.8x and 1.3x (`ClipFitMinRate`, `ClipFitMaxRate` on the melee component): a
  wind-up too long for 1.3x starts part way in, one too short for 0.8x holds its first frame (he
  squares up) for the difference. Kate's clips play at rate 1.
- Thug clip timings (2026-09-28; trims in the manifest, measured on the district's thugs by
  `Hawkeye.Smoke.ThugClipsStrikeOnTheTelegraph`): fists (Jab To Elbow first since 2026-09-29, 0.92x,
  no hold; then Standing Melee Punch, 0.25 s of wind-up, which holds 0.29 s then plays at 0.8x) land
  at 0.60 s; bat (Standing Melee Attack Horizontal, 0.87 s of
  wind-up, one clip for the bat, bash and slow swing) plays at 1.3x from 0.09 s in and lands at 0.60 s;
  bash 1.09x, 0.80 s; slow swing 0.87x, 1.00 s. Each swing is over (montage ended) 0.48 to 0.72 s after
  its hit. A clip knockdown falls within 1.4 s and holds on the floor; the finisher's victim clip
  starts at the sweep, so he is off his feet before he is thrown at 0.35 s.
- Motion warping (the engine's MotionWarping plugin, the component the sample already puts on
  Kate): a full-body strike clip with root motion is warped toward the soft-lock target, the
  warp target `CombatTarget` placed 90 cm short of him and facing him; with no target the warp
  target is removed and the clip's own root motion plays. Such a clip replaces the lunge; an
  in-place clip (no root motion) keeps the lunge.
- Dodge, finisher, hit reactions: the rules (300 cm in 0.4 s, 0.25 s invulnerable; 1.2 s
  finisher, lethal at 0.35 s) stay in C++. A dodge clip for the stick's direction relative to
  her facing plays fitted to DodgeSeconds, pose only (the dash moves her). The finisher plays
  FinisherAttacker (FinisherBow with the bow up) fitted to DurationSeconds and FinisherVictim on
  the thug at rate 1 until he is thrown. Kate's stagger and a thug's hit reaction play the Hit
  clip for the side the blow came from, on top of the lean.
- Knockdown with clips: a heavy strike or a bola trip plays Knockdown, which holds its last
  frame, instead of the ragdoll; after KnockdownSeconds GetUp plays and he is getting up for
  its length (with no GetUp clip the knockdown clip blends out over GetUpSeconds). The
  explosive's blast always ragdolls. A clip knockdown counts as knocked down for the finisher
  gate exactly as the ragdoll does.
- Bow with clips (upper body, over locomotion; rewritten 2026-09-28 after "the bow animation is not
  natural"): BowDraw plays at its own pace, rate 1, whatever the bow's draw time or DrawRate, and holds
  its last frame (the anchor); once it is at its end BowAimIdle takes over and loops until the release.
  The draw fraction (damage, speed, spread, the perfect window, the HUD) still counts on its own clock,
  FullDrawSeconds over DrawRate, so the pose never waits on the number or the number on the pose.
  Release plays BowFire; a new draw during it starts BowDraw at once. A let-down, cancel or a release
  below MinDrawFraction blends the clip out over 0.25 s. Bow clips blend in over 0.15 s and out over
  0.25 s on the UpperBody slot (spine_01 up); the legs stay on locomotion.
  Sparrow's clips as trimmed in the manifest: BowDraw is Primary_Fire_Med 0.43-0.93 s (the hand from the
  arrow rest back to the anchor, 0.50 s; the pull itself 0.33 s), BowAimIdle Sparrow's drawn idle (a
  10 s loop that starts on the anchor pose), BowFire Primary_Fire_Med 0.00-0.43 s (the snap, then the
  hand back to the string, which is where BowDraw starts). RMB_Drawback is not used: it is a 2.5 s
  charged draw, and fitted to 0.8 s it rushed and blended out before the aim clip came in.
- A clip holds the arms exactly as much as it is blended in: the IK's clip alpha is the UpperBody slot's
  weight (UBowComponent::GetBowClipWeight), so the bow hand's IK is off by that weight and the bow rides
  in the clip's bow hand. The bow stays in the hand while a bow clip is still blended in above 0.05,
  even after the follow-through; it goes on the back only once the arms are the locomotion's again.
  A shot on a fire clip follows through only until that clip starts to blend out (its length less its
  blend-out, 0.18 s for Sparrow's), never longer than FollowThroughSeconds, so the IK does not lift the
  arm back up while the clip lowers it. Without a fire clip the 0.6 s follow-through stands.
- The string hand under a clip stays on the clip. The IK only corrects it toward the arrow line: the
  line back from the bow through where the arrow will fly (the launch direction, from the bow hand
  socket to the aim point), at the clip's own draw length, so the correction never fights the pull. Its
  alpha is the draw alpha times the correction: 0 within ClipCorrectionDeadZone (3 cm) of the line,
  rising smoothly to ClipCorrectionMaxAlpha (0.5) at ClipCorrectionFullDistance (15 cm). The draw
  elbow's pole is the clip's own elbow, so the elbow never swings to a hint. The nock and the arrow
  follow the string hand as before, so the arrow is always on the string.
- Aim offset (2026-09-28): `BS_BowAimOffset_Sparrow` per skeleton, an AimOffset blend space built by
  the import script from Sparrow's AO_idle layout and its nine idle_AO poses retargeted (additive in
  mesh space on the centre pose). It plays on the UpperBody branch after the slot, at alpha clip alpha
  times bow alpha, so only while a bow clip holds the arms (the IK-only bow never gets it). Its inputs:
  yaw is the aim's yaw off the actor's, pitch the aim's pitch (the control rotation for the player, the
  aim override for the AI), each clamped to the blend space's own axis range and to
  MaxAimOffsetYaw/Pitch (90). Under it the spine's turn toward the aim is not added (the aim offset
  turns the body); with no aim offset asset the old turn stays.
- Aim offset pitch (2026-09-29): the pitch input is the aim's pitch times `AimOffsetPitchScale` (1.57)
  above level or `AimOffsetPitchScaleDown` (1.85) below, plus `AimOffsetPitchBias` (5), then clamped
  as above. Sparrow's up and down poses aim far less than their +-90 on the axis, not alike, its centre
  pose holds the arrow 4 degrees low, and the string hand's correction takes back part of any change.
  Measured by `Hawkeye.Screenshot.BowDraw` (nocked arrow pitch, standalone): before, 23.9 at 30 up,
  -25.2 at 30 down, -4.0 level; after, 30.6, -30.2 and -1.5. Looking down, the launch line from the
  bow hand to the aim point is shallower than the view (-19.3 at 30 down), so there the arrow points
  with the view rather than along its flight; the arrow still flies to the reticle. Clint shares
  Kate's skeleton and settings; the archers take the same scales on their own aim offset.
