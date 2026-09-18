// Mirrors the knob: balance = brew credit - cups. The first pot of a brew
// event is worth 4 cups; extra pots in the same event only 2 each (brewing
// a second pot is barely more hassle once you're already at it).
export const FIRST_POT_CUPS = 4;
export const EXTRA_POT_CUPS = 2;

export function brewCredit(pots: number) {
  if (pots <= 0) return 0;
  return FIRST_POT_CUPS + (pots - 1) * EXTRA_POT_CUPS;
}

export type EventRow = {
  personId: string;
  personName?: string;
  kind: "brew" | "cup";
  quantity: number;
  office?: string;
};

// Events written before multi-office support carry no office stamp.
const DEFAULT_OFFICE = "Oslo";

export function officeOf(ev: EventRow) {
  return ev.office || DEFAULT_OFFICE;
}

export type Standing = {
  personId: string;
  name: string;
  pots: number;
  cups: number;
  credit: number;
  balance: number;
};

export function aggregate(events: readonly EventRow[]): Standing[] {
  const byPerson = new Map<string, Standing>();
  for (const ev of events) {
    let s = byPerson.get(ev.personId);
    if (!s) {
      s = {
        personId: ev.personId,
        name: ev.personName || "Unknown",
        pots: 0,
        cups: 0,
        credit: 0,
        balance: 0,
      };
      byPerson.set(ev.personId, s);
    }
    if (ev.kind === "brew") {
      s.pots += ev.quantity ?? 1;
      s.credit += brewCredit(ev.quantity ?? 1);
    } else if (ev.kind === "cup") {
      s.cups += ev.quantity ?? 1;
    }
  }
  const standings = [...byPerson.values()];
  for (const s of standings) s.balance = s.credit - s.cups;
  return standings.sort(
    (a, b) =>
      b.balance - a.balance ||
      b.pots - a.pots ||
      a.personId.localeCompare(b.personId),
  );
}
