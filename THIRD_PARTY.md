# External components

This repository contains the utility source code. Game assets, translation projects,
localization patches, GGUF models, llama.cpp binaries and fonts are not included.

- **TranslateGemma**: download a compatible 4B GGUF separately. Official model:
  https://huggingface.co/google/translategemma-4b-it. The model uses Google's Gemma
  terms; the MIT license for this utility does not license the model.
- **llama.cpp / ggml**: https://github.com/ggml-org/llama.cpp, MIT. The original
  integration was tested with snapshot `bd4eeaa047006cb1fe71999fbd11134b5836e167`.
  The server must support TranslateGemma's Jinja template and language arguments.
- **Qt 6**: system libraries linked dynamically, subject to Qt's own license:
  https://www.qt.io/licensing/opensource-lgpl-obligations.
- **SQLite / zlib**: system dependencies supplied through Qt SQLite and zlib.
- **Fonts**: users select their own TTF/OTF. Noto Sans is one compatible choice,
  licensed separately under the SIL Open Font License:
  https://github.com/notofonts/latin-greek-cyrillic.

Fable is owned by its respective rights holders. This is an unofficial tool.
