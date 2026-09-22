# Legacy

This directory contains the previous NightMare Network implementation for historical reference and migration context.

It is **not** part of the active library and is not a model for new code.

## Current implementation

The authoritative current implementation lives in:

```text
src/
```

Current documentation lives in:

```text
docs/
```

Supported examples live in:

```text
examples/
```

When investigating current behavior, use the active `src/` implementation and current documentation.

## Why Legacy is kept

Legacy code can still be useful for:

- understanding an older NightMare-based project,
- identifying how a previous API or protocol behaved,
- documenting migration from the previous architecture,
- understanding why a current design decision exists.

Do not use `Legacy/` to infer current behavior merely because a similar implementation exists there.

If current documentation and active source disagree, `src/` determines current behavior and the documentation should be corrected.
