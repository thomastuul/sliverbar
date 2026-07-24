# Deployment validation

Deployment changes require explicit approval and must preserve the user's
current runtime behavior.

## Before replacing the live instance

- Run CTest successfully.
- Capture the current panel as the visual baseline.
- Record the complete configuration used by the running process.
- Start the candidate with the same complete configuration.
- Put feature-specific test settings in a configuration copy instead of using
  a minimal configuration.

## Visual comparison

Compare the candidate and baseline across the complete panel and every changed
popup. Check:

- font family and size;
- glyph selection;
- colors, including active and warning states;
- spacing and alignment;
- module order;
- panel geometry;
- popup geometry.

Treat every unexplained visual difference as a regression that must be fixed
before deployment.

## Runtime boundaries

- Run the production panel only as a normal process in the user's graphical
  session, never inside the development container.
- Use host tools such as `xprop`, `xrandr`, `xdotool`, `xwininfo`, `bspc`,
  `pactl`, `nmcli`, `ps`, and `strace` only for relevant integration tests or
  runtime diagnosis.
- Restore the previous live state after temporary validation changes.
- A full `codex-security` scan is optional and requires explicit user approval.
