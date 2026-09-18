import { aggregate, officeOf, type EventRow } from "../lib/coffee-stats.ts";

export type CoffeeEvent = EventRow & { occurredAt: string };
export type ReportOptions = {
  /** Quarters and years follow Sanity's February–January fiscal calendar. */
  period: "week" | "month" | "quarter" | "year";
  /** Any office-local calendar date in the desired period (YYYY-MM-DD). */
  date: string;
  /** Last local calendar day to include; omit for a complete period. */
  through?: string;
  office: string;
  timeZone: string;
  /** Current directory names, keyed by personId. Snapshots are the fallback. */
  names?: ReadonlyMap<string, string>;
};

function calendarDate(value: string) {
  const date = new Date(value + "T00:00:00Z");
  if (
    !/^\d{4}-\d{2}-\d{2}$/.test(value) ||
    !Number.isFinite(date.getTime()) ||
    date.toISOString().slice(0, 10) !== value
  )
    throw new Error("date must be a valid YYYY-MM-DD calendar date");
  return date;
}

function dateKey(date: Date) {
  return date.toISOString().slice(0, 10);
}

function monday(date: Date) {
  const start = new Date(date);
  start.setUTCDate(start.getUTCDate() - ((start.getUTCDay() + 6) % 7));
  return start;
}

function periodBounds(options: ReportOptions) {
  const start = calendarDate(options.date);
  const fiscalMonth = (start.getUTCMonth() + 11) % 12;
  const fiscalYear =
    start.getUTCFullYear() + (start.getUTCMonth() === 0 ? 0 : 1);
  const fiscalLabel = `FY${String(fiscalYear).slice(-2)}`;
  const periodLabel =
    options.period === "quarter"
      ? `Q${Math.floor(fiscalMonth / 3) + 1} ${fiscalLabel}`
      : options.period === "year"
        ? fiscalLabel
        : options.period.toUpperCase();
  if (options.period === "week")
    start.setUTCDate(start.getUTCDate() - ((start.getUTCDay() + 6) % 7));
  else {
    start.setUTCDate(1);
    if (options.period === "quarter")
      start.setUTCMonth(start.getUTCMonth() - (fiscalMonth % 3));
    if (options.period === "year")
      start.setUTCMonth(start.getUTCMonth() - fiscalMonth);
  }
  const end = new Date(start);
  if (options.period === "week") end.setUTCDate(end.getUTCDate() + 7);
  else
    end.setUTCMonth(
      end.getUTCMonth() + { month: 1, quarter: 3, year: 12 }[options.period],
    );
  const periodEnd = new Date(end);
  if (options.through) {
    const through = calendarDate(options.through);
    if (through < start || through >= end)
      throw new Error("through must fall within the report period");
    end.setTime(through.getTime());
    end.setUTCDate(end.getUTCDate() + 1);
  }
  return { start, end, periodLabel, toDate: end < periodEnd };
}

export function buildCoffeeReport(
  events: readonly CoffeeEvent[],
  options: ReportOptions,
) {
  if (!options.office.trim()) throw new Error("office is required");
  const localDay = new Intl.DateTimeFormat("en-CA", {
    timeZone: options.timeZone,
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  });
  const { start, end, periodLabel, toDate } = periodBounds(options);
  const from = dateKey(start);
  const to = dateKey(end);
  const monthly = options.period === "year";
  const weekly = options.period === "quarter";
  const bucketKey = (day: string) =>
    monthly
      ? day.slice(0, 7)
      : weekly
        ? dateKey(monday(calendarDate(day)))
        : day;
  const label = new Intl.DateTimeFormat("en-US", {
    timeZone: "UTC",
    ...(monthly
      ? { month: "short" }
      : weekly
        ? { month: "short", day: "numeric" }
        : options.period === "week"
          ? { weekday: "short" }
          : { day: "numeric" }),
  });
  const buckets = new Map<
    string,
    { label: string; pots: number; cups: number; partial: boolean }
  >();
  for (const cursor = new Date(start); cursor < end;) {
    const next = weekly ? monday(cursor) : new Date(cursor);
    if (monthly) next.setUTCMonth(next.getUTCMonth() + 1);
    else next.setUTCDate(next.getUTCDate() + (weekly ? 7 : 1));
    buckets.set(bucketKey(dateKey(cursor)), {
      label: label.format(cursor),
      pots: 0,
      cups: 0,
      partial: weekly && (cursor > monday(cursor) || next > end),
    });
    cursor.setTime(next.getTime());
  }
  const selected: CoffeeEvent[] = [];
  for (const event of events) {
    if (officeOf(event) !== options.office) continue;
    const occurredAt = new Date(event.occurredAt);
    if (
      !/T.*(?:Z|[+-]\d{2}:\d{2})$/i.test(event.occurredAt) ||
      !Number.isFinite(occurredAt.getTime())
    )
      throw new Error("occurredAt must be an ISO timestamp with a timezone");
    const day = localDay.format(occurredAt);
    if (day < from || day >= to) continue;
    if (
      !event.personId ||
      !["brew", "cup"].includes(event.kind) ||
      !Number.isSafeInteger(event.quantity) ||
      event.quantity < 1
    )
      throw new Error(
        "Coffee events need a personId, brew/cup kind, and positive integer quantity",
      );
    const bucket = buckets.get(bucketKey(day));
    if (!bucket) throw new Error("Event falls outside the report buckets");
    bucket[event.kind === "brew" ? "pots" : "cups"] += event.quantity;
    selected.push(event);
  }
  // Latest snapshot wins regardless of the query's ordering.
  selected.sort((a, b) => Date.parse(b.occurredAt) - Date.parse(a.occurredAt));
  const standings = aggregate(selected).map((person) => ({
    ...person,
    name: options.names?.get(person.personId) || person.name,
  }));
  const lastDay = new Date(end);
  lastDay.setUTCDate(lastDay.getUTCDate() - 1);
  const dateLabel = new Intl.DateTimeFormat("en-US", {
    timeZone: "UTC",
    month: "short",
    day: "numeric",
    year: "numeric",
  });
  return {
    period: options.period,
    periodLabel,
    toDate,
    office: options.office,
    timeZone: options.timeZone,
    from,
    to,
    dateLabel: dateLabel.formatRange(start, lastDay),
    standings,
    buckets: [...buckets]
      .filter(
        ([day]) =>
          monthly || weekly || ![0, 6].includes(calendarDate(day).getUTCDay()),
      )
      .map(([, bucket]) => bucket),
    pots: standings.reduce((sum, person) => sum + person.pots, 0),
    cups: standings.reduce((sum, person) => sum + person.cups, 0),
    participants: standings.length,
    brewers: standings.filter((person) => person.pots > 0).length,
  };
}

export type CoffeeReport = ReturnType<typeof buildCoffeeReport>;
