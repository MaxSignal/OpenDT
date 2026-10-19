# Clean-room records (historical)

This directory preserves the working records from the clean-room development of OpenDT. They are kept to
document **how** the implementation was produced. They are not user or developer documentation, and they
describe the state of the project at the time they were written (October 2026). For current documentation, see
the [README](../../README.md), [CONTRIBUTING.md](../../CONTRIBUTING.md) and
[docs/architecture/](../architecture/README.md).

## Process

1. **Specification team.** A separate team studied the original software and wrote a natural-language
   specification (in Japanese) describing *what* the player must do: data formats, screen layout, timing,
   formulas, the order of random-number draws and so on. It contains no code. The specification itself is not
   part of this repository.
2. **Implementation team.** OpenDT was implemented using only:
   - the specification (v1.0, later v1.1),
   - the data files of sample games (images, maps, databases, MML music, SOUND.DAT),
   - black-box observation of the original games running in DOSBox-X (PC-98 mode): watching the screen and
     sending keys, with no debugging, memory inspection or disassembly,
   - general-purpose third-party components (ymfm, Shinonome fonts).

   The original executables were not disassembled or decompiled. The only byte-level inspection was what the
   specification explicitly allows: reading fixed header bytes of RPG.EXE to detect its build, and searching
   MUSIC.COM for its version string. No source code or documentation of other reimplementations was consulted.
3. **Questions and answers.** Wherever the specification was ambiguous or contradictory, the implementers did
   not guess. They recorded a question, marked any stopgap code with `PROVISIONAL(Qn)`, and sent the collected
   questions to the specification team. The team answered all 50 questions and published spec v1.1. All answers
   were then applied, and no `PROVISIONAL` markers remain.
4. **Verification.** Each module was checked against the acceptance points in the specification, against
   reference screenshots and audio supplied with it (all 200 reference screenshots matched pixel for pixel, and
   all three reference audio recordings matched sample for sample), and by independent reviewers who compared
   the specification with the code section by section.

## Files

| File | Contents |
|---|---|
| [process-rules.md](process-rules.md) | Rules the implementers worked under (allowed sources, prohibitions, how to record questions) |
| [emulator-observation.md](emulator-observation.md) | How the original games were observed in DOSBox-X, with known limitations |
| [spec-questions.md](spec-questions.md) | All 50 questions sent to the specification team, with the priority list |
| [spec-answers-v1.1.md](spec-answers-v1.1.md) | The specification team's answers, which accompanied spec v1.1 |
| [questions/](questions/) | The per-module question logs (music, D1, D2 data/field, D2 events, D2 menus, D2 battle) with resolutions |

## Notes

- These documents were originally written in Japanese and have been translated into English. Quotations of
  on-screen text and of Japanese specification terms are kept in Japanese, with English glosses.
- Files were renamed when they were moved here: `DEVELOPMENT.md` → `process-rules.md`,
  `ORIGINAL_OBSERVATION.md` → `emulator-observation.md`, `SPEC_QUESTIONS.md` → `spec-questions.md`,
  `SPEC_ANSWERS_v1_1.md` → `spec-answers-v1.1.md`, `questions_<module>.md` → `questions/<module>.md`.
- The project was called "Dante98CR" during development and was renamed OpenDT before publication.
- Paths, build directories and tool names mentioned in these records refer to the development environment of
  the time and may not match the current repository.
