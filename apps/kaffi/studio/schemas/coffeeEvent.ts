import { AddCircleIcon } from "@sanity/icons/AddCircle";
import { RemoveCircleIcon } from "@sanity/icons/RemoveCircle";
import { defineField, defineType } from "sanity";

import { PersonIdInput } from "../components/PersonIdInput";

// One immutable record per action. Stats are derived from this log via GROQ,
// never stored as counters. `kind` discriminates brews from cups so the core
// "pots brewed vs cups consumed" query is a single collection scan.
//
// liveEdit: the device writes published documents straight through the
// mutation API — a draft stage would only get in the way of quick fixes.
export const coffeeEvent = defineType({
  name: "coffeeEvent",
  title: "Coffee event",
  type: "document",
  icon: AddCircleIcon,
  liveEdit: true,
  fields: [
    defineField({
      name: "kind",
      type: "string",
      options: {
        layout: "radio",
        list: [
          { title: "Brewed a pot", value: "brew" },
          { title: "Took a cup", value: "cup" },
        ],
      },
      initialValue: "cup",
      validation: (rule) => rule.required(),
    }),
    // Ideally a `globalDocumentReference` into Sanity Home
    // (r3dzy7he.production) — but the Content Lake currently rejects
    // dataset-scoped GDRs ("only supported by Media Library"), so the id is
    // stored raw and the input resolves it live instead.
    defineField({
      name: "personId",
      title: "Person",
      type: "string",
      description: "The Sanity Home person _id this event belongs to.",
      components: { input: PersonIdInput },
      validation: (rule) => rule.required(),
    }),
    defineField({
      name: "personName",
      title: "Name snapshot",
      type: "string",
      readOnly: true,
      description:
        "Denormalized at write time for readability. Sanity Home is the " +
        "source of truth — the device and the leaderboard always resolve " +
        "the current name by personId.",
    }),
    defineField({
      name: "quantity",
      type: "number",
      description: "Pots (1 or 2) for a brew, cups for a cup.",
      initialValue: 1,
      validation: (rule) => rule.required().min(1).integer(),
    }),
    defineField({
      name: "occurredAt",
      title: "Occurred at",
      type: "datetime",
      initialValue: () => new Date().toISOString(),
      validation: (rule) => rule.required(),
    }),
  ],
  orderings: [
    {
      title: "Newest first",
      name: "occurredAtDesc",
      by: [{ field: "occurredAt", direction: "desc" }],
    },
  ],
  preview: {
    select: {
      kind: "kind",
      quantity: "quantity",
      person: "personName",
      occurredAt: "occurredAt",
    },
    prepare: ({ kind, quantity, person, occurredAt }) => {
      const count = quantity ?? 1;
      const who = person ?? "Someone";
      const noun = kind === "brew" ? "pot" : "cup";
      const verb = kind === "brew" ? "brewed" : "took";
      return {
        title: `${who} ${verb} ${count} ${noun}${count === 1 ? "" : "s"}`,
        subtitle: occurredAt
          ? new Date(occurredAt).toLocaleString()
          : undefined,
        // Semantic, not literal: a brew adds to the office coffee pool, a
        // cup takes from it — mirrors the balance math (pots × 2 − cups).
        media: kind === "brew" ? AddCircleIcon : RemoveCircleIcon,
      };
    },
  },
});
