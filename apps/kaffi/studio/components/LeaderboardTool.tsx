import {
  Avatar,
  Badge,
  Box,
  Card,
  Container,
  Flex,
  Spinner,
  Stack,
  Text,
} from "@sanity/ui";
import { useEffect, useMemo, useState } from "react";
import { useClient } from "sanity";

import { API_VERSION, useDirectoryPeople } from "../lib/directory";

// Mirrors the knob: balance = brew credit - cups. The first pot of a brew
// event is worth 4 cups; extra pots in the same event only 2 each (brewing
// a second pot is barely more hassle once you're already at it).
const FIRST_POT_CUPS = 4;
const EXTRA_POT_CUPS = 2;

function brewCredit(pots: number) {
  if (pots <= 0) return 0;
  return FIRST_POT_CUPS + (pots - 1) * EXTRA_POT_CUPS;
}

type EventRow = {
  personId: string;
  personName?: string;
  kind: "brew" | "cup";
  quantity: number;
};

type Standing = {
  personId: string;
  name: string;
  pots: number;
  cups: number;
  credit: number;
  balance: number;
};

const MEDAL_TONES = ["caution", "default", "primary"] as const;

function yearStartIso() {
  return `${new Date().getFullYear()}-01-01T00:00:00Z`;
}

function aggregate(events: EventRow[]): Standing[] {
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
  return standings.sort((a, b) => b.balance - a.balance || b.pots - a.pots);
}

export function LeaderboardTool() {
  const client = useClient({ apiVersion: API_VERSION });
  const [events, setEvents] = useState<EventRow[] | null>(null);
  const [error, setError] = useState(false);

  useEffect(() => {
    client
      .fetch<EventRow[]>(
        '*[_type == "coffeeEvent" && occurredAt >= $ys]{personId, personName, kind, quantity}',
        { ys: yearStartIso() },
      )
      .then(setEvents)
      .catch(() => setError(true));
  }, [client]);

  const standings = useMemo(() => (events ? aggregate(events) : []), [events]);
  const { people } = useDirectoryPeople(standings.map((s) => s.personId));

  if (error)
    return (
      <Container width={1} padding={4}>
        <Card padding={4} radius={3} tone="critical">
          <Text>Couldn't load coffee events.</Text>
        </Card>
      </Container>
    );

  if (!events)
    return (
      <Flex align="center" justify="center" padding={6}>
        <Spinner muted />
      </Flex>
    );

  return (
    <Container width={1} padding={4}>
      <Stack space={4}>
        <Stack space={2}>
          <Text size={3} weight="bold">
            Coffee heroes
          </Text>
          <Text size={1} muted>
            This year — a brew is worth {FIRST_POT_CUPS} cups (+{EXTRA_POT_CUPS}{" "}
            for a second pot), minus cups taken. Live from the event log; names
            and photos come from Sanity Home.
          </Text>
        </Stack>

        {standings.length === 0 && (
          <Card padding={4} radius={3} border>
            <Text muted>Nothing brewed yet — be the first hero.</Text>
          </Card>
        )}

        <Stack space={2}>
          {standings.map((s, rank) => {
            const person = people.get(s.personId);
            const worst = rank === standings.length - 1 && s.balance < 0;
            return (
              <Card
                key={s.personId}
                padding={3}
                radius={3}
                border
                tone={worst ? "critical" : "default"}
              >
                <Flex align="center" gap={3}>
                  <Box style={{ width: "1.6em", textAlign: "center" }}>
                    {rank < 3 ? (
                      <Badge tone={MEDAL_TONES[rank]} radius={6}>
                        {rank + 1}
                      </Badge>
                    ) : (
                      <Text size={1} muted>
                        {rank + 1}
                      </Text>
                    )}
                  </Box>
                  <Avatar src={person?.image} size={1} />
                  <Stack space={2} flex={1}>
                    <Text size={2} weight="semibold">
                      {person?.name ?? s.name}
                    </Text>
                    <Text size={1} muted>
                      {s.pots} {s.pots === 1 ? "pot" : "pots"} · {s.cups}{" "}
                      {s.cups === 1 ? "cup" : "cups"}
                    </Text>
                  </Stack>
                  <Text
                    size={2}
                    weight="bold"
                    style={{
                      color:
                        s.balance > 0
                          ? "#3ab667"
                          : s.balance < 0
                            ? "#f36458"
                            : undefined,
                    }}
                  >
                    {s.balance > 0 ? `+${s.balance}` : s.balance}
                  </Text>
                </Flex>
              </Card>
            );
          })}
        </Stack>
      </Stack>
    </Container>
  );
}
