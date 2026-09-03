# Operation guide

This guide covers everyday use of the Paper Mono NFC Image Transfer phone app
and M5Stack Paper Mono C153. See [installing release binaries](install_binary.en.md)
before using the product for the first time.

Use the language icon at the top right of the phone app to choose `日本語` or
`English`. Japanese is the first-launch default, and the selection persists
across app restarts.

## 1. Physical buttons

A hold is recognized after about 700 ms.

| Screen | BtnA | BtnB |
| --- | --- | --- |
| Dashboard | Hold: open menu | Press: enter/leave low-power lock |
| Menu | Press: next; hold: dashboard | Press: open |
| NFC waiting | Hold: cancel | Not used |
| Image Library | Press: next; hold: menu | Press: display; hold: delete mode |
| Library delete mode | Press: next; hold: cancel delete mode | Press: mark; hold: delete marked |
| Step Goal | Press: -1,000; hold: back | Press: +1,000; hold: save |
| Step History | Press: next page; hold: back | Press: previous page |
| Reset Image | Press: next; hold: back | Press: confirm |

The power key remains reserved for device power control. Touch is disabled while
a FULL image is displayed, but holding BtnA and pressing BtnB still work.

## 2. Send an image

Image transfer and clock synchronization are independent. You do not need to
set the clock before sending, and sending an image never changes device time.

1. Choose a layout under `表示レイアウト` (Display layout) in the phone app:
   - `時計と表示` (Clock + image): a 386 x 386 DASH image shown with clock,
     date, calendar, and steps.
   - `全画面` (Full screen): a 480 x 800 FULL image with no dashboard overlay.
2. Pick an image from the gallery or camera.
3. Adjust the crop and generate the monochrome preview.
4. Hold BtnA on the Paper Mono dashboard and open `RECEIVE IMAGE`.
5. Start NFC sending in the app and place the phone's NFC antenna against the
   Paper Mono NFC area.
6. Keep it in place until the app reports that the image is stored.

After storage completes, the progress indicator stops and the completion panel
closes automatically after about two seconds. Error panels remain until closed
manually so that the cause can be reviewed.

If communication is interrupted, retrying the same preview resumes from the
stored offset. Choosing a different image or layout prepares a new transfer.
Paper Mono leaves NFC waiting mode after about 60 seconds.

## 3. Set the clock

1. Open `SYNC CLOCK` in the Paper Mono menu.
2. Tap `NFCで時刻を同期` (Sync clock over NFC) in the app.
3. Hold the phone against the NFC area until completion.

The app sends UTC time and the phone's current UTC offset. Paper Mono then uses
the resulting local date for daily step records and returns directly to the
dashboard without an intermediate completion screen. No image needs to be selected.

## 4. Image Library

The library keeps up to 17 received images plus the protected default image,
for 18 cards total. Each page is a two-column, three-row grid of six cards, with
at most three pages.

- Received images are newest first; the default image is last.
- `NEW` marks the newest image and `OK` identifies the currently displayed image.
- A thick black border identifies the focus moved by BtnA `NEXT`.
- `DASH` means an image shown with the dashboard; `FULL` means full-screen.
- No image name is transferred, so received cards use newest-first numbers.
- Committing an 18th received image automatically evicts the oldest received one.

### Choose the displayed image

- Tap a thumbnail to display it and return to the dashboard.
- Change pages with a horizontal swipe, bottom arrows, or page dots.
- With buttons, press BtnA to advance and BtnB to display the focused card.
- Hold BtnA to return directly to the dashboard without reopening the menu.

### Delete images

1. Tap the header `DELETE` button or hold BtnB to enter delete mode.
2. Tap thumbnails, or press BtnB, to mark multiple received images.
3. Tap `DELETE n`, or hold BtnB, to delete the marked images.

Use `CANCEL` or hold BtnA to discard the marks. The default image shows `LOCK`
and cannot be deleted. Deleting the active received image selects the default.
The normal footer shows `HOLD A: BACK` followed by `HOLD B: DELETE`.

## 5. Reset Image

`RESET IMAGE` contains three actions:

| Action | Result |
| --- | --- |
| `USE DEFAULT` | Select the default without deleting received images |
| `DELETE SELECTED` | Open Image Library multi-select deletion |
| `DELETE ALL RECEIVED` | Delete every received image after confirmation |

These actions never remove the embedded default, clock, time zone, step history,
or step goal.

## 6. Step goal and history

`STEP GOAL` supports 1,000–50,000 steps in 1,000-step increments. Tap `-`/`+`
or press BtnA/BtnB, then tap `SAVE` or hold BtnB.

`STEP HISTORY` keeps 30 days and shows seven days per page. Use BtnA/BtnB for
the next/previous page, and tap the footer or hold BtnA to return. `AVG` is the
average of saved daily records in the displayed month.

Steps are estimated from acceleration, so carrying position and gait can cause
some difference from a phone. Calibration changes apply only to newly detected
steps; existing saved history is left unchanged.

## 7. Low-power lock

Press BtnB on the dashboard to enter low-power lock without altering the image.
No extra `LOW POWER` text is drawn over the display.

- The front light, touch, NFC, RGB LED, buzzer, and gyroscope stop.
- RTC operation and accelerometer step counting continue.
- A DASH view partially refreshes clock and steps about once per minute.
- A FULL view remains unchanged with no overlay or partial refresh.

The battery value at the upper right is an estimate mapping 3.3–4.2 V to
0–100%. Voltage filtering reduces visible fluctuation, and a transient read
failure keeps the last valid value.

Press BtnB again to restore normal operation, the front light, and touch.

## 8. Troubleshooting

- Image sending reports a clock-sync error: update both app and firmware to the
  same current Release.
- NFC is not detected: match the Paper Mono waiting screen to the app action.
- Transfer disconnects: locate the phone's NFC antenna and keep it still.
- Android reports `Tag is out of date`: the current app automatically hands
  off to the replacement NFC tag. Keep the phone against Paper Mono until the
  completion status appears.
- A new image is missing: check the first library page for the `NEW` badge.
- Restore the initial artwork: choose `RESET IMAGE` → `USE DEFAULT`.
