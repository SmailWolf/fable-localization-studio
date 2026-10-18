#!/bin/sh
set -eu
FABLE_STUDIO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$FABLE_STUDIO_ROOT/bin/fable-localization-studio" "$@"
