# Clean-room specification (v1.1, Japanese original)

This is the natural-language specification that OpenDT was implemented from. The specification team wrote it
in Japanese, without any code, describing *what* a player for Dante98 (D1) and Dante98 II (D2) games must do. The
implementation team worked only from this text (v1.0, then v1.1), the sample game data and black-box
observation; see [../README.md](../README.md) for the process.

It is published here unchanged except for the points listed under [Editorial changes](#editorial-changes).

## Contents

| File | Topic |
|---|---|
| [00_README.md](00_README.md) | Purpose, clean-room rules, scope, reading order, conventions, glossary, sample games, compatibility criteria, change history |
| [01_platform_common.md](01_platform_common.md) | Screen model, text and fonts, timing, input, RNG, file access, game detection and launcher, audio output framework, headless runner |
| [02_music_driver.md](02_music_driver.md) | MUSIC.COM-compatible sound driver: MML compiler, sequencer, SOUND.DAT effects, YM2203 register writes, versions |
| [10_d1_data_formats.md](10_d1_data_formats.md) | D1 data files and save format |
| [11_d1_engine.md](11_d1_engine.md) | D1 title, field, events, menus, saving |
| [12_d1_battle.md](12_d1_battle.md) | D1 battle |
| [19_d1_messages.md](19_d1_messages.md) | D1 built-in system messages |
| [20_d2_data_formats.md](20_d2_data_formats.md) | D2 data files, save format, RPG.EXE build detection and per-build differences |
| [21_d2_flow_field.md](21_d2_flow_field.md) | D2 game flow, maps, drawing, movement, NPCs, vehicles, encounters |
| [22_d2_events.md](22_d2_events.md) | D2 event script commands |
| [23_d2_menus.md](23_d2_menus.md) | D2 common rules, field menu, shops, inns, debug menu |
| [24_d2_battle.md](24_d2_battle.md) | D2 battle |
| [90_testing.md](90_testing.md) | Acceptance test plan and milestones |
| [95_public_references.md](95_public_references.md) | Notes gathered from public sources (MML reference, manuals) |
| [SPEC_ANSWERS_v1.1.md](SPEC_ANSWERS_v1.1.md) | The specification team's answers to the implementers' 50 questions (an English translation is in [../spec-answers-v1.1.md](../spec-answers-v1.1.md); the questions are in [../spec-questions.md](../spec-questions.md)) |

Passages changed in v1.1 are marked 「（v1.1 で修正）」 (fixed in v1.1) or 「（v1.1 で追記）」 (added in v1.1).

## Editorial changes

- **Reference implementation.** The specification team worked from an internal, non-public reference
  implementation built from analysis of the original programs. Its name has been replaced throughout by
  「参照実装」 ("the reference implementation"). Where the specification described strings that contained that
  program's name (the launcher's title, the window title and the screenshot file name), the name is replaced by
  「アプリ名」 / 「＜アプリ名＞」 ("the application name"); OpenDT uses "OpenDT" there.
- **Reference screenshots and audio are not included.** The specification refers to `reference_shots/` (five
  screenshots for each of 40 sample games) and `reference_audio/` (three recordings). They are captures of
  third-party games and are not redistributed here. `tools/run_all_refs.sh` compares against them when you
  have a copy (`OPENDT_REF`).
- **Sample games are not included** either; the specification refers to them under `samples/`.

Everything else, including the clean-room rules in 00_README.md §1 as they were given to the implementers, is
the original text.
