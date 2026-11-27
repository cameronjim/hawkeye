# Recording guide: your three clips

You record three clips by hand. Each is 10 to 15 seconds raw; the cut uses 6 to 8. Record a few takes of
each and keep the best.

| File | Used for | Raw length | Start it with |
|------|----------|------------|---------------|
| `street_fight.mp4` | Shot 10: group combat | 12 to 15 s | `.\Tools\demo-record.ps1 -Clip street_fight` |
| `stealth_takedown.mp4` | Shot 11: stealth | 10 to 12 s | `.\Tools\demo-record.ps1 -Clip stealth_takedown` |
| `tomas_phase.mp4` | Shot 12: the Tomas phase change | 12 to 15 s | `.\Tools\demo-record.ps1 -Clip tomas_phase` |

Run it from anywhere, with the full path (a PowerShell window in any folder):

```powershell
& "C:\Users\camer\code\hawkeye\Tools\demo-record.ps1" -Clip street_fight
& "C:\Users\camer\code\hawkeye\Tools\demo-record.ps1" -Clip stealth_takedown
& "C:\Users\camer\code\hawkeye\Tools\demo-record.ps1" -Clip tomas_phase
```

Each one drops you at that clip's start (the game also passes `-HawkeyeDemoStart=<clip>`), so there is nothing to find and
nothing to walk to: the action is in front of you when the game opens. The game says where it put you in one line of
`Saved\Logs\Hawkeye.log` (search "HawkeyeDemoStart"); if it says it could not, tell me what the line says. The chapter
title card and the "New objective" toast show first; wait them out before you record. For another take, or after you die, close
the game and run the command again: the start is applied once per launch (a death reloads the level at the chapter's
own start).

Drop the three files in `C:\Users\camer\code\hawkeye\Saved\Demo\owner\` (make the folder if it is not there).
Those exact names; the cutter looks for them. Until they are there, `python Tools\Demo\cut.py --placeholder` makes a
rough cut with a labelled black slate for each.

## Your campaign save

Playing by hand (as opposed to a test or the demo capture, which use their own slots) writes your own campaign:
the roaming autosave and every objective save to `Saved\SaveGames\HawkeyeCampaign.sav`, and your settings go to
`HawkeyeSettings.sav`. `Tools\demo-record.ps1` is the way to start each clip because it:

1. copies both files to `Saved\Demo\save-backup\<time>\` before the game starts;
2. starts the game with `-NoMainMenu`, so "Continue" cannot load your campaign into the clip's chapter, with the
   chapter flag for the clip (`-HawkeyeChapter=DA_CH03_Grotto`, `-HawkeyeChapter=DA_CH04_SocialClub`; the asset
   names are the ones the laps start) and with `-HawkeyeDemoStart=<clip>`, which puts you at the clip's start;
3. waits until you close the game, then copies the files back. If you had no campaign save, the one the clip made is
   deleted. Your campaign is exactly as it was.

If the script is killed or the machine crashes mid-clip, put the saves back by hand:
`.\Tools\demo-record.ps1 -Restore Saved\Demo\save-backup\<time>`.
Do not start the game any other way for these clips, and do not start the full editor (16 GB is not enough for it
and a recording at once).

## Before you start

1. Close the editor, the browser and anything else heavy. 16 GB is tight, and a stutter shows in the video.
2. The script starts the game fullscreen at 1920x1080 (add `-Windowed` for a window the same size).
3. Set the NVIDIA overlay once. Press `Alt+Z`, open Settings, then Video capture:
   - Resolution: 1920x1080 (if your display is bigger, record "In-game" not "Desktop").
   - Frame rate: 60 FPS.
   - Bitrate: High, 50 Mbps or more.
   - Format: H.264 MP4 if it offers a choice.
   - Save the captures to a folder you can find, such as `C:\Users\camer\Videos\Hawkeye`.
   The recording key is usually `Alt+F9` (start and stop). If your version of the NVIDIA app uses other keys,
   the overlay's Settings page lists them.
4. Game settings: Night (the default). Turn the music down or off in the pause menu if it talks over the
   sound effects; the video has no voiceover. Leave the HUD on in these three clips, so health and the prompts read.
   Do not open photo mode (F11) or press F12 (a note), and leave the console shut.
5. Check the frame rate: if the game is under 55 fps in the spot, fix that first (close apps, drop a setting),
   do not record it.

Keys: Shift sprint, Ctrl crouch and dodge, V strike (hold for a heavy), F takedown and finisher, Q grapple, E interact,
M map, Esc pause.

The fight controls, the ones the clips use: **tap V on the "!" to parry** (the "!" is over a thug who is about to hit you;
tap as it shows, not before), **Ctrl while moving to dodge**, **hold V for a heavy strike**, **F for the takedown from behind
and for the finisher** (the prompt shows when it is open).

## Clip 1: street fight (`street_fight.mp4`)

Where: `demo-record.ps1 -Clip street_fight` starts the district with no chapter running (the chase the district opens on
is put away, so nothing runs off or reloads) and drops you on the Avenue A sidewalk, about 9 m from the two thugs who
patrol the avenue (a bat and a gunner with a pistol), looking at them. The heavy with the shield walks his own 20 m of
the sidewalk behind you, about 40 m to the west, so he arrives last, after the pair. There is no crime marker and no
objective on the screen; it is free roam. The same spot `Hawkeye.Lap.StreetFight` stands the lap bot on. The pair notice
you within a few seconds; if they do not, shoot an arrow at the gunner or step toward them.

What to do, in order:
1. Start recording with the pair in view and a clear 5 m around you.
2. Let the bat man start his swing, parry or dodge it so the telegraph reads.
3. Land two or three light strikes, then one heavy (hold V).
4. Dodge a second swing from the side. Show the group take turns rather than all swing at once.
5. Finish one thug with the finisher (F) if it opens. Stop recording a second after he drops.

Keep the camera behind Kate. Do not open the map or inventory. Do not die. The lap bot wins this fight in about 17
seconds with no hits taken; allow yourself a minute.

## Clip 2: stealth takedown (`stealth_takedown.mp4`)

Where: chapter 3, the Grotto. `demo-record.ps1 -Clip stealth_takedown` starts the chapter (the same chapter
`Hawkeye.Lap.Grotto` plays) and drops you on the street about 9 m in front of the grotto's door (the candy-striped posts,
the "photos" sign over it, the string lights), facing it, with the pavement along the building to either side. The
objective is "Find the stolen sleigh display": the volume that completes it is a few steps in front of you, so walking
toward the door completes it, and the next objective, "Get into Santa's grotto without being seen", puts its marker on the
door itself. Walk to the door and press `E`: it takes you into `L_Int_Grotto` (a short fade), and the first elf you can
take is in the hall, as in the lap, which takes him from behind and then creeps past two elves talking in the stockroom.
An elf who sees you spoils the take.

What to do:
1. Start recording crouched (Ctrl) in the hall with the elf in view and unaware.
2. Move up behind him, crouched, until the "[F] from behind to take him down" prompt shows.
3. Press `F` for the takedown. Let the animation finish.
4. Stay crouched for a second after, so it reads that nobody noticed. Stop recording.

A guard who turns round or an alert "!" ruins the clip. Close the game (the script restores your save) and start the
clip again; the chapter always begins at its start.

## Clip 3: the Tomas phase change (`tomas_phase.mp4`)

Where: chapter 4, the social club. `demo-record.ps1 -Clip tomas_phase` starts the chapter and puts you on the club's roof,
2.5 m or more in from the parapet where the street grapple lands, facing Tomas's crew (four of his guys standing about
the roof, and Tomas himself further back, watching). You are on the roof, so
the first objective, "Go to the social club roof", completes by itself as the game sees your feet on it, the pre-fight
lines play (up to 45 s; hit one of the crew to cut them short) and then the fight starts. If the game could not find
a free spot on the roof it puts you on the street stand under the roof's parapet instead, looking up at the grapple anchor
(the log line says "street stand"): aim at the anchor, press `Q`, and you land on the roof as in the lap. The fight has
three phases: the crew (four of Tomas's guys, two candy-cane bats, fists, a shield; he watches), Tomas, and the Santa under
40% of Tomas's health. The change you want is the first: when the crew is down, Tomas steps in after a 1.5 s protected beat.

What to do:
1. Start recording as the last of the crew goes down, with Tomas in view.
2. Keep fighting through the transition: the camera looks over Kate's shoulder at him for about 2.5 s and he speaks.
3. Parry one of his jabs, dodge the haymaker (it is unblockable), and land a hit while he is winded (after three
   swings he is winded for 3 s; the finisher only works in that window).
4. Stop recording after the first full exchange, about 12 to 15 s after the change.

Leave Tomas alone while he is only watching (the crew phase); the lap bot never hits him then. The Santa's
phase is a separate, later clip.

## After each take

1. Stop the recording, open the file, and watch it once. Check: it is 1920x1080 at 60 fps (right-click, Properties,
   Details), there is no stutter, nothing personal shows (no notifications, no chat, no desktop).
2. Rename it to the exact filename above and move it into `Saved\Demo\owner\`.
3. Tell me the best take's in and out seconds if the good part is not at the start. I set `start` in the list.
4. Check `Saved\SaveGames\HawkeyeCampaign.sav` is the file you expect (the script says "restored" for it).

## Then

Run `python Tools\Demo\cut.py` for the real cut. It checks the length (75 to 90 s) and that every source exists.
