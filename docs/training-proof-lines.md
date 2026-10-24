# Training lines in the worldserver log

All lines are written to the `module.overseer` log channel.

## Success: one proof line per purchase

A rank or recipe that a character actually bought from a trainer writes one
line, after the module has read back that the character now holds it (the core
reports a failed sale only to a client, so the read-back is the only proof):

```
overseer: trainer_purchase member='<name>' kind=<rank|recipe> skill=<trade> (<id>) spell=<id> '<spell name>' max_skill=<n> paid_copper=<n>
```

- `kind` is `rank` for a trade or tier (Journeyman, Expert) and `recipe` for a
  recipe, whether bought with the rank, on its own, or named by the roster.
- `max_skill` is the character's ceiling in that trade after the sale.
- `paid_copper` is what the purse actually lost, at the trainer's price. A
  grant would read 0 and is not a purchase.
- It is never written when a walk is planned, when the sale is refused, or
  when the read-back finds the character did not learn it.

The shape is pinned by `tests/test_trainer_purchase_line.cpp`. A recipe
purchase also writes an `overseer_event` row of kind `learn`, as before.

## Refusal: beside it

A named recipe the trainer refused is logged as `was refused recipe <id> by
'<trainer>' (creature <n>): <word>`, where the word is one of `not_sold`,
`cannot_teach` or `too_poor`. A sale that went through TeachSpell but left the
character without the spell is logged as `was not taught`.
