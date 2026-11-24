# Recording guide: your three clips

You record three clips by hand. Each is 10 to 15 seconds raw; the cut uses 6 to 8. Record a few takes of
each and keep the best.

| File | Used for | Raw length |
|------|----------|------------|
| `street_fight.mp4` | Shot 10: group combat | 12 to 15 s |
| `stealth_takedown.mp4` | Shot 11: stealth | 10 to 12 s |
| `tomas_phase.mp4` | Shot 12: the Tomas phase change | 12 to 15 s |

Drop the three files in `C:\Users\camer\code\hawkeye\Saved\Demo\owner\` (make the folder if it is not there).
Those exact names; the cutter looks for them. Until they are there, `python Tools\Demo\cut.py --placeholder` makes a
rough cut with a labelled black slate for each.

## Before you start

1. Close the editor, the browser and anything else heavy. 16 GB is tight, and a stutter shows in the video.
2. Run the build from the packaged or standalone game, never the editor window:
   `.\Tools\play.ps1 -Fullscreen` starts the game fullscreen at 1920x1080. For the chapter clips use the
   command line in the clip's section.
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
   Turn off any photo or debug overlay.
5. Check the frame rate: if the game is under 55 fps in the spot, fix that first (close apps, drop a setting),
   do not record it.

## Clip 1: street fight (`street_fight.mp4`)

Where: the Avenue A sidewalk in the district, where `Hawkeye.Lap.StreetFight` plays: the street group of a bat,
a gunner and a heavy. Use the world map (`M`) to find the crime marker on that stretch if you do not see them. The
exact spot comes from the lap test's description; check it on the day.

What to do, in order:
1. Start recording with the thugs in view and a clear 5 m around you.
2. Let the bat man start his swing, parry or dodge it so the telegraph reads.
3. Land two or three light strikes, then one heavy (hold the strike key).
4. Dodge a second swing from the side. Show the group take turns rather than all swing at once.
5. Finish one thug with the finisher if it opens. Stop recording a second after he drops.

Keep the camera behind Kate. Do not open the map or inventory. Do not die.

## Clip 2: stealth takedown (`stealth_takedown.mp4`)

Where: chapter 3, the Grotto (`L_Int_Grotto`). Start the chapter from the safehouse's chapter select, or launch with
`-HawkeyeChapter=DA_CH03_Grotto` (the mission asset's name, as the laps use it). Pick a guard with his back turned.

What to do:
1. Start recording standing in cover (crouched) with the guard in view and unaware.
2. Move up behind him, crouched, so the "[F] from behind" prompt shows.
3. Press `F` for the takedown. Let the animation finish.
4. Stay crouched for a second after, so it reads that nobody noticed. Stop recording.

A guard who turns round or an alert "!" ruins the clip. Reload from the last save and retake.

## Clip 3: the Tomas phase change (`tomas_phase.mp4`)

Where: chapter 4, the social club. Start with `-HawkeyeChapter=DA_CH04_SocialClub` on the command line, or pick
the chapter in the safehouse's chapter select. Fight the crew first. The phase changes when the crew is down: Tomas
steps in after a short protected beat. The clip needs that change.

What to do:
1. Start recording as the last of the crew goes down, with Tomas in view.
2. Keep fighting through the 1.5 s transition: the camera looks over Kate's shoulder at him and he speaks.
3. Parry one of his jabs, dodge the haymaker (it is unblockable), and land a hit while he is winded.
4. Stop recording after the first full exchange, about 12 to 15 s after the change.

If you want Santa's phase too, that is a separate, later clip. This one is the first change only.

## After each take

1. Stop the recording, open the file, and watch it once. Check: it is 1920x1080 at 60 fps (right-click, Properties,
   Details), there is no stutter, nothing personal shows (no notifications, no chat, no desktop).
2. Rename it to the exact filename above and move it into `Saved\Demo\owner\`.
3. Tell me the best take's in and out seconds if the good part is not at the start. I set `start` in the list.

## Then

Run `python Tools\Demo\cut.py` for the real cut. It checks the length (75 to 90 s) and that every source exists.
