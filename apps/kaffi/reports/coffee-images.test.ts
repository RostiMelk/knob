import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { expect, test } from "vitest";
import {
  buildCoffeeReport,
  type CoffeeEvent,
  type ReportOptions,
} from "./report-data.ts";
import {
  renderCoffeeSummary,
  renderCoffeeTrend,
  renderCoffeeLeaderboard,
} from "./coffee-images.ts";

const options: ReportOptions = {
  period: "week",
  date: "2026-09-16",
  office: "San Francisco",
  timeZone: "America/Los_Angeles",
};
const event = (overrides: Partial<CoffeeEvent> = {}): CoffeeEvent => ({
  personId: "alex",
  personName: "Alex Rivera",
  kind: "brew",
  quantity: 1,
  office: options.office,
  occurredAt: "2026-09-14T16:00:00Z",
  ...overrides,
});

test("office-local periods preserve brew-action credits, names, and empty days", () => {
  const report = buildCoffeeReport(
    [
      event({ quantity: 2 }),
      event({ occurredAt: "2026-09-15T16:00:00Z" }),
      event({ kind: "cup", quantity: 3 }),
      event({
        personId: "jo",
        personName: "Jo Chen",
        kind: "cup",
        quantity: 2,
      }),
      event({ quantity: 100, occurredAt: "2026-09-14T06:59:59Z" }),
      event({ quantity: 100, occurredAt: "2026-09-21T07:00:00Z" }),
      event({ quantity: 100, office: undefined }),
    ],
    { ...options, names: new Map([["alex", "Alex & Jo <team>"]]) },
  );
  expect([
    report.from,
    report.to,
    report.pots,
    report.cups,
    report.brewers,
    report.participants,
  ]).toEqual(["2026-09-14", "2026-09-21", 3, 5, 1, 2]);
  expect(report.standings[0]).toMatchObject({
    name: "Alex & Jo <team>",
    credit: 10,
    balance: 7,
  });
  expect(report.buckets.map((bucket) => bucket.label)).toEqual([
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
  ]);
  expect(report.buckets[0]).toMatchObject({ label: "Mon", pots: 2, cups: 5 });
  expect(report.buckets[4]).toMatchObject({ label: "Fri", pots: 0, cups: 0 });
  expect(
    buildCoffeeReport([event({ office: undefined })], {
      ...options,
      office: "Oslo",
      timeZone: "Europe/Oslo",
    }).pots,
  ).toBe(1);
});

test("calendar buckets cover leap months, quarters, year rollover, and DST", () => {
  for (const [period, date, from, to, count] of [
    ["month", "2024-02-20", "2024-02-01", "2024-03-01", 21],
    ["quarter", "2026-11-16", "2026-10-01", "2027-01-01", 14],
    ["year", "2026-12-31", "2026-01-01", "2027-01-01", 12],
  ] as const) {
    const report = buildCoffeeReport([], { ...options, period, date });
    expect([report.from, report.to, report.buckets.length]).toEqual([
      from,
      to,
      count,
    ]);
  }
  const dst = buildCoffeeReport(
    [
      event({ occurredAt: "2026-03-09T06:59:59Z" }),
      event({ occurredAt: "2026-03-09T07:00:00Z" }),
    ],
    { ...options, date: "2026-03-08" },
  );
  expect(dst.buckets.every((bucket) => bucket.pots === 0)).toBe(true);
  expect(dst.pots).toBe(1);
  const friday = buildCoffeeReport(
    [
      event({ occurredAt: "2026-09-19T06:59:59Z" }),
      event({ occurredAt: "2026-09-19T07:00:00Z" }),
    ],
    options,
  );
  expect(friday.buckets[4].pots).toBe(1);
  expect(friday.pots).toBe(2);
});

test("quarter-to-date totals clip to the quarter and cutoff, retaining weekends in weekly buckets", () => {
  const quarter = {
    ...options,
    period: "quarter",
    through: "2026-09-18",
  } satisfies ReportOptions;
  const report = buildCoffeeReport(
    [
      event({ occurredAt: "2026-07-01T06:59:59Z", quantity: 100 }),
      event({ occurredAt: "2026-07-01T07:00:00Z" }),
      event({ occurredAt: "2026-09-13T16:00:00Z", kind: "cup", quantity: 2 }),
      event({ quantity: 2 }),
      event({ occurredAt: "2026-09-18T16:00:00Z", kind: "cup", quantity: 3 }),
      event({ occurredAt: "2026-09-19T07:00:00Z", quantity: 100 }),
    ],
    quarter,
  );
  expect([
    report.from,
    report.to,
    report.toDate,
    report.pots,
    report.cups,
  ]).toEqual(["2026-07-01", "2026-09-19", true, 3, 5]);
  expect(report.buckets).toHaveLength(12);
  expect(report.buckets[0]).toEqual({
    label: "Jul 1",
    pots: 1,
    cups: 0,
    partial: true,
  });
  expect(report.buckets[10]).toEqual({
    label: "Sep 7",
    pots: 0,
    cups: 2,
    partial: false,
  });
  expect(report.buckets[11]).toEqual({
    label: "Sep 14",
    pots: 2,
    cups: 3,
    partial: true,
  });
  expect(() =>
    buildCoffeeReport([], { ...quarter, through: "2026-10-01" }),
  ).toThrow("within the report period");
  expect(() =>
    buildCoffeeReport([], { ...quarter, through: "2026-06-30" }),
  ).toThrow("within the report period");
  expect(renderCoffeeTrend(report).readUInt32BE(16)).toBe(1200);
});

test("invalid dates, timestamps and quantities fail instead of producing misleading stats", () => {
  expect(() =>
    buildCoffeeReport([], { ...options, date: "2026-02-30" }),
  ).toThrow("calendar date");
  expect(() =>
    buildCoffeeReport([], { ...options, timeZone: "not-a-zone" }),
  ).toThrow();
  expect(() =>
    buildCoffeeReport([event({ occurredAt: "2026-09-14T16:00:00" })], options),
  ).toThrow("timezone");
  for (const quantity of [0, -1, 1.5, NaN, Infinity])
    expect(() => buildCoffeeReport([event({ quantity })], options)).toThrow(
      "positive integer",
    );
});

test("ties are stable and name snapshots use timestamp order across UTC offsets", () => {
  const report = buildCoffeeReport(
    [
      event({ personName: "Old", occurredAt: "2026-09-14T09:00:00+02:00" }),
      event({ personName: "New", occurredAt: "2026-09-14T08:00:00Z" }),
      event({ personId: "bea", personName: "Bea" }),
      event({ personId: "bea" }),
    ],
    options,
  );
  expect(
    report.standings.map((person) => [
      person.personId,
      person.balance,
      person.name,
    ]),
  ).toEqual([
    ["alex", 8, "New"],
    ["bea", 8, "Bea"],
  ]);
});

const sampleNames = [
  "Alex Rivera",
  "Jo Chen",
  "Sam Okafor",
  "Robin Berg",
  "Drew Patel",
  "Casey Silva",
  "Taylor Kim",
  "Morgan Lee",
];
const sampleEvents = sampleNames.flatMap((name, person) =>
  [0, 1, 2, 3, 4].flatMap((day) => {
    const base = {
      personId: String(person),
      personName: name,
      occurredAt: `2026-09-${14 + day}T17:00:00Z`,
    };
    return [
      event({ ...base, kind: "cup", quantity: 1 + ((person + day) % 5) }),
      ...(person < 6 && (person + day) % 3 !== 0
        ? [event({ ...base, quantity: (person % 2) + 1 })]
        : []),
    ];
  }),
);
const renderers = {
  summary: renderCoffeeSummary,
  leaderboard: renderCoffeeLeaderboard,
  trend: renderCoffeeTrend,
};

test("all three generators produce valid PNGs for populated and empty periods", () => {
  const report = buildCoffeeReport(sampleEvents, options);
  const directory = process.env.KAFFI_PREVIEW_DIR;
  if (directory) mkdirSync(directory, { recursive: true });
  for (const [name, render] of Object.entries(renderers)) {
    for (const [variant, data] of [
      ["sample", report],
      ["empty", buildCoffeeReport([], options)],
      [
        "long-name",
        buildCoffeeReport(
          [
            event({
              personName:
                "Ægir & Zoë <team> / A very long colleague name that should fit within the row",
            }),
          ],
          options,
        ),
      ],
      ["cups-only", buildCoffeeReport([event({ kind: "cup" })], options)],
    ] as const) {
      const png = render(data);
      expect(png.subarray(0, 8).toString("hex")).toBe("89504e470d0a1a0a");
      expect([png.readUInt32BE(16), png.readUInt32BE(20)]).toEqual([1200, 800]);
      expect(png.length).toBeGreaterThan(10000);
      if (directory)
        writeFileSync(join(directory, `${name}-${variant}.png`), png);
    }
  }
  expect(renderCoffeeSummary(report)).toEqual(renderCoffeeSummary(report));
  if (directory) {
    for (const period of ["month", "quarter", "year"] as const)
      writeFileSync(
        join(directory, `trend-${period}.png`),
        renderCoffeeTrend(
          buildCoffeeReport(sampleEvents, { ...options, period }),
        ),
      );
  }
});
