import {
  Avatar,
  Badge,
  Box,
  Card,
  Container,
  Flex,
  Spinner,
  Stack,
  Tab,
  TabList,
  Text,
} from "@sanity/ui";
import { useEffect, useMemo, useState } from "react";
import { useClient } from "sanity";

import { API_VERSION, useDirectoryPeople } from "../lib/directory";

import {
  aggregate,
  officeOf,
  FIRST_POT_CUPS,
  EXTRA_POT_CUPS,
  type EventRow,
} from "../../lib/coffee-stats";

const MEDAL_TONES = ["caution", "default", "primary"] as const;

function yearStartIso() {
  return `${new Date().getFullYear()}-01-01T00:00:00Z`;
}

export function LeaderboardTool() {
  const client = useClient({ apiVersion: API_VERSION });
  const [events, setEvents] = useState<EventRow[] | null>(null);
  const [error, setError] = useState(false);
  const [office, setOffice] = useState<string | null>(null);

  useEffect(() => {
    client
      .fetch<EventRow[]>(
        '*[_type == "coffeeEvent" && occurredAt >= $ys]{personId, personName, kind, quantity, office}',
        { ys: yearStartIso() },
      )
      .then(setEvents)
      .catch(() => setError(true));
  }, [client]);

  const offices = useMemo(
    () => [...new Set((events ?? []).map(officeOf))].sort(),
    [events],
  );
  const selectedOffice = office ?? offices[0];

  const standings = useMemo(
    () =>
      events
        ? aggregate(events.filter((ev) => officeOf(ev) === selectedOffice))
        : [],
    [events, selectedOffice],
  );
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

        {offices.length > 1 && (
          <TabList space={1}>
            {offices.map((o) => (
              <Tab
                key={o}
                id={`office-${o}`}
                aria-controls="office-standings"
                label={o}
                selected={o === selectedOffice}
                onClick={() => setOffice(o)}
              />
            ))}
          </TabList>
        )}

        {standings.length === 0 && (
          <Card padding={4} radius={3} border>
            <Text muted>Nothing brewed yet — be the first hero.</Text>
          </Card>
        )}

        <Stack space={2} id="office-standings">
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
