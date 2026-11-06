# AutoKey KWin Bridge

A KWin plugin that gives [AutoKey](https://github.com/dlk3/autokey-wayland) what it
needs on KDE Plasma Wayland, without root or uinput: the keyboard events and hotkey
grabs that Wayland keeps from normal clients.

It runs inside KWin and offers the `org.autokey.KWinBridge` D-Bus interface:

| Method | What it does |
|---|---|
| `Register` | Start sending key events to the caller |
| `SetHotkeys` | Grab these key combinations |
| `ClearModifiers` / `RestoreModifiers` | Release held modifiers while typing |
| `StartMonitor` / `StopMonitor` | Send all input, for recorders |

AutoKey connects to it automatically when the plugin is loaded, and otherwise falls
back to uinput. [kwin-input](https://github.com/SamuelCGilbert/kwin-input) uses it
too, for `monitor` and `--clearmodifiers`.

**Any program on your session bus can use this interface while the plugin is loaded,
which includes reading what you type.** Only install it if you're comfortable with
that.

## Build

KWin has no stable plugin API, so the plugin must be built against the headers of
the exact KWin version you run, and rebuilt after every Plasma update.

Needs `kwin-dev`, `qt6-base-dev`, `qt6-declarative-dev` and `libxkbcommon-dev`.

```sh
cmake -B build
cmake --build build
sudo cmake --install build
```

Log out and back in. The plugin is enabled by default. To turn it off, set
`autokeybridgeEnabled=false` in the `[Plugins]` group of `~/.config/kwinrc`.

To install without root, copy `autokeybridge.so` to
`~/.local/opt/kwin-plugins/kwin/plugins` and set
`QT_PLUGIN_PATH=~/.local/opt/kwin-plugins` for your Plasma session.

## License

GPL-3.0-or-later
