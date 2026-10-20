# HP2 RPG mod

RPG layer for the macOS source port of *Harry Potter and the Chamber of Secrets*
([cefege/harry-potter-2-chamber-of-secrets-pc](https://github.com/cefege/harry-potter-2-chamber-of-secrets-pc)):

- **House points**: live "Gryffindor loses 5 points" messages, house crests with totals on the HUD,
  and a penalty for hitting students with spells.
- **Experience and levels**: XP for creatures, wizard cards, challenge stars, new spells and house points.
  Each level gives a skill point.
- **Skills** (press **K**): Health, Speed, Jump and Luck, 5 points each.
- **Shop** (press **B** near a student or Fred/George): potions, ingredients, wizard cards, clothes,
  hats, glasses, gear and extra lessons, paid in Bertie Bott's beans.
- **Wardrobe** (press **K**, then Tab): house uniforms, Quidditch robes, hats, glasses and held items.

RPG progress is stored in `SaveN.rpg` next to each `SaveN.usa`; retail data and saves stay untouched.
Turn it off with `[HP2Rpg] Enabled=False` in the game ini.

`overlay/` holds the changed source files. The `build` workflow checks out the upstream port at a
pinned commit, copies `overlay/` over it and uploads a DMG as a workflow artifact.

Console commands: `RPG STATUS`, `RPG XP <n>`, `RPG BEANS <n>`, `RPG HAT <up> <forward> <side> <scale>`.
