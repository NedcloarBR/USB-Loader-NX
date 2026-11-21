# Manager themes

A theme changes the Manager's colors and sounds. Copy its folder, or its `.zip`, to `usbloadernx/themes/` on the SD card and pick it in **Settings → Manager → Theme**, a row of previews; A applies the focused one right away.

```text
usbloadernx/themes/<name>/
├── theme.json
├── thumb.png            optional: the theme picker's preview (or thumb.jpg, or the file in Images.Thumbnail; 320x180)
├── background.jpg       optional, named in theme.json (JPEG or PNG; 1920x1080 for a full screen)
├── music.ogg            optional, named in theme.json (OGG, MP3 or WAV; loops)
└── sfx/                 optional: any of these replace the built-in sound
    ├── move.wav  select.wav  back.wav  whoosh.wav  open.wav
    ├── toggle_on.wav  toggle_off.wav  launch.wav  error.wav
    └── plug.wav  unplug.wav
```

## theme.json

```json
{
	"Format": { "Version": "1" },
	"Theme": { "Name": "Midnight", "Author": "NedcloarBR", "Version": "1.0" },
	"Colors": { "Primary": "#7C8CFF", "Ground": "#0F1424" },
	"Images": { "Background": "background.jpg", "BackgroundMode": "stretch" },
	"Sounds": { "Music": "music.ogg" },
	"Animations": { "Speed": "1.0", "Floating": "on", "Particles": "on", "FocusPulse": "1.6" }
}
```

`Format.Version` is the version of this format the theme was made for (`"1"` here, and for a theme without it). A Manager that only knows an older format still loads what it understands and marks the theme in the picker as made for a newer version. `Theme.Version` is the theme's own version, whatever its author likes.

A `<name>.zip` is unpacked into `<name>/` when the Manager starts, and again whenever the `.zip` is newer; `theme.json` can be at the archive's root or in one folder at its root. The `.zip` stays where it is.

Anything left out keeps the default, so a theme can change a single color. A `theme.json` that does not parse is ignored as a whole. File names are relative to the theme's folder and cannot leave it.

| Key | Values |
|---|---|
| `Theme.Name` | Shown in the theme picker (the folder name otherwise) |
| `Colors.*` | `#RRGGBB` or `#RRGGBBAA`, see below |
| `Images.Thumbnail` | The preview in the theme picker (16:9, 320x180 is enough); without it `thumb.png`, `thumb.jpg`, then the background are tried, and with none of them the picker sketches the screen in the theme's colors |
| `Images.Background` | Drawn behind everything, over the `Ground` color |
| `Images.BackgroundMode` | `stretch` (default) or `tile` |
| `Images.BackgroundDim` | `0` to `0.9`: how much of `Ground` covers the background image so text stays readable (default `0.4`); over an image, the text areas also get a veil of `Ground` |
| `Sounds.Music` | Background music, under the sound effects; the player can turn it off in the settings |
| `Animations.Speed` | `0.25` to `4`: how fast transitions run (`1` as designed) |
| `Animations.Floating` | `off` keeps covers and icons still |
| `Animations.Particles` | `off` removes the drifting specks |
| `Animations.FocusPulse` | `0.5` to `6`: seconds for the focus ring to go from `Focus` to `FocusAlt` and back (default `1.6`); `off` keeps it still |

| Color | Used for |
|---|---|
| `Ground` | Screen background, and the gap between a focused item and its ring |
| `Panel` | Windows, cards, chips |
| `Raised` | Selected cards and sections, cards inside windows |
| `Track` | Lines, borders, empty bars, switches turned off |
| `Text`, `TextSoft` | Text and button glyphs, and softer text and icons |
| `Muted`, `MutedSoft`, `Faint` | Secondary text, from strongest to faintest |
| `Primary` | The app mark, the main buttons, switches turned on |
| `OnPrimary` | Text and glyphs on `Primary` |
| `Focus`, `FocusAlt` | The focus ring pulses between the two, with a glow |
| `Info` | Links |
| `Star` | Favorites |
| `Warning`, `Pending`, `Error` | With `Primary`, the drive usage bar: `Primary` up to 60%, then these up to 100% |
| `ErrorText` | Almost full drives, errors |
| `Alert`, `AlertSurface`, `AlertText` | What needs attention: an update on another drive, fragmentation (line, box, box text) |
| `Overlay` | Over the screen behind a window (use `#RRGGBBAA`) |

Sound effects are WAV files; short, quiet ones work best. [midnight](midnight/) is an example with every option; its background, preview and music are made by `tools/assets/generate_example_theme.py`.
