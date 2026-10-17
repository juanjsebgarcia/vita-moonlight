# Translating Moonlight for PS Vita

This is the brief for an agent (or a person) translating the app into one language, or updating a translation after the English changed. Run one translator per language. Each works only on its own `lang/<tag>.ini` and `tools/translate/glossary/<tag>.md`.

## 1. What the app is

Moonlight for PS Vita streams games from a gaming PC to a PlayStation Vita or PS TV over Wi-Fi. The PC runs **Sunshine** (the host), and the Vita runs Moonlight (the client). Players pair the Vita with their PC once, pick a game, and play it on the Vita's screen with the Vita's controls.

The people reading the text are **players**, not engineers. They're holding a handheld, often on a sofa or in bed, often on so-so Wi-Fi. They want to get into a game quickly, and to understand what a setting does in plain words. Many are homebrew enthusiasts, so they know their way around, but they shouldn't need networking knowledge.

The screen is 960x544, the menus use a monospaced font at 18 px, and rows are about 60 characters wide **including the value on the right**. Long text overlaps.

## 2. Tone

- Plain, short, friendly and direct, like the system menus of a games console. Not marketing, not jokey, not stiff.
- Address the player the way **PlayStation's own system software does in your language**, as on the PS4 and PS5: for example "vous" in French, "du" in German, "tú" in Spanish, "você" in Brazilian Portuguese, and polite neutral forms in Japanese, Korean and Chinese. If you aren't sure what Sony uses, say so in your report. Be consistent across every string, and record the choice in your glossary.
- Prefer the words **Sony uses on the PS Vita, PS4 and PS5** for the same thing: Settings, Back, Wi-Fi, Bluetooth, Network, PS button, touchpad, rear touch pad, front touch screen, controller, screen, standby, and so on. If Sony's term for your language is known, use it; otherwise use the most common gaming term.
- Gaming terms that players in your language use in English (often "FPS", "V-Sync", "bitrate", "stream", "lag") may stay in English if that is what gamers say. Otherwise translate them. Decide once, record it in your glossary, and keep to it.
- Help texts (keys ending `_HELP`) explain what a setting does and when to change it. Keep their meaning and advice exactly; you may restructure sentences so they read naturally.

## 3. Rules that must not be broken

1. **Placeholders** `{0}`, `{1}` … `{9}` are filled in by the app (names, numbers, addresses). Keep every one, exactly as written. You may move them to wherever your grammar needs them. The note on each key says what each one is.
2. **Button markers** in braces, such as `{confirm}`, `{cancel}`, `{cross}`, `{circle}`, `{triangle}`, `{square}`, `{start}`, `{select}`, `{l}`, `{r}`, `{left}`, `{right}`, `{ps}`, are drawn as the button's symbol. Keep every one. Never spell a button out ("X", "Triangle", "START") where the English uses a marker, and never translate the word inside the braces. `{confirm}` and `{cancel}` already follow the player's button layout.
3. **Line breaks** are written `\n`. Keep the same number, in the same places relative to the content.
4. **Never translate**: Moonlight, Sunshine, PlayStation, PS Vita, Vita, PS TV, GameStream, GeForce Experience, GFE, Xbox, DS4, Wi-Fi, Bluetooth, GPU, IP, MAC, PIN, H.264, URLs, file paths, numbers, resolutions (960x544), and the key names LB, RB, LS, RS, LT, RT, Esc, Tab, F1 to F12, L1, R1, L2, R2. "Desktop" in `REC_NO_DESKTOP_APP` is filled in by the app; leave the placeholder as it is.
5. **Units** after a number (`{0} Mbps`, `{0} kbps`, `{0} s`, `{0} bytes`, `{0}px`) may be localised if your language normally writes them differently (Russian "Мбит/с", French puts a space before %, and so on). Keep the placeholder.
6. **Length**:
   - `TAB_*` (Settings tabs): at most 10 characters each, and about 41 for all five together, as they share one line with the clock.
   - Keys whose note says "keep short" (hints beside buttons, values like "(fits now)"): no longer than the English, give or take a few characters.
   - Settings rows (`SET_*` without `_HELP`): aim for no more than 45 characters, since the value sits on the same line.
   - Settings messages (`SET_NOTE_*`) and other lines whose note gives a limit: keep within it. They're a single line that doesn't wrap.
   - Everything else wraps, so length matters less, but don't pad.
7. **No printf codes** (`%s`, `%d`). If you see one in the English, report it; don't copy it.
8. **Keep the same set of keys.** Don't add keys that aren't in the template. Leaving a key out makes it show in English: only do that if you truly can't translate it, and say so in your report.

## 4. Fonts

The app's font covers Latin (including all Western and Central European accents) and Cyrillic. Japanese, Korean and Chinese use the Vita's system font, set by `font` in `[meta]`. Write correct text in your language regardless; never replace a letter with a look-alike to suit the font. Turkish needs ğ, Ğ and İ: write them properly.

## 5. The files

- **Your output:** `lang/<tag>.ini`, UTF-8, in this shape:

  ```ini
  ; Moonlight for PS Vita: <language name in English> translation
  [meta]
  tag = <tag>
  name = <the language's name, in the language itself>
  font = <latin | cjk-ja | cjk-ko | cjk-zh-hans | cjk-zh-hant>

  [strings]
  KEY = translated text
  ```

  Give every key from the work file, in the template's order, one per line, `KEY = value`. No quotes. A `;` at the start of a line is a comment; anywhere else it's part of the text.
- **Your glossary:** `tools/translate/glossary/<tag>.md`. A short table of your decisions: how you address the player, and your term for each recurring concept (settings, stream, host/PC, pair, bitrate, frame rate, resolution, connection, Wi-Fi signal, touch screen, rear touch pad, button names, controller). On an update, **read it first and follow it**; add to it, don't contradict it.
- **Your input:** the work file from `tools/translate/prepare.py <tag>` (see §7), which lists each key to translate with its English, the translator's note, and the current translation if there is one.

## 6. How to work

1. Read this whole brief, then your glossary if it exists, then the work file.
2. Skim the whole English text before writing, so you know the app's vocabulary and how strings relate (row label, then its help; a message and its button).
3. Translate. Where a note explains a placeholder, make sure the sentence works for every value it can take.
4. **Self-review**, as a native-speaking gamer would: read every string again. Is it natural? Does it match Sony's terms and your glossary? Are the placeholders and markers all there? Are tabs and hints short enough? Would a player understand each help text without the English?
5. Run `python3 -I tools/i18n_check.py` and fix every error it reports for your file. Then run `python3 -I tools/i18n_widths.py`, which estimates the width of each line that doesn't wrap (menu rows with their values, the tab bar), and shorten any of yours it reports.
6. Write or update your glossary.
7. Report: the language, how many strings you translated, any you left in English and why, any English that looked wrong or ambiguous, and any choices a reviewer should check.

## 7. Updating a translation later

When the English changes, run `python3 -I tools/translate/prepare.py <tag>`. It writes `tools/translate/work/<tag>.md` with only the keys that are new, or whose English changed since `lang/source.ini` was last recorded, each with the current translation for reference. Translate those keys with this brief, merge them into `lang/<tag>.ini` (keep every other line as it is), and run the check. When every language is done, record the new English with `python3 -I tools/translate/prepare.py --snapshot`.
