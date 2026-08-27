import { Avatar, Badge, Card, Flex, Stack, Text } from "@sanity/ui";
import { useFormValue, type StringInputProps } from "sanity";

import { useDirectoryPeople } from "../lib/directory";

// Custom input for coffeeEvent.personId: renders the raw id field, then
// live-resolves the person from Sanity Home so the id is verifiable at a
// glance — and flags when the stored name snapshot has drifted from the
// directory.
//
// A native `globalDocumentReference` would replace this, but the Content
// Lake currently only accepts GDRs for Media Library resources — plain
// `dataset:` refs are rejected by the mutation API. Revisit when GDRs for
// datasets go GA.
export function PersonIdInput(props: StringInputProps) {
  const personId = typeof props.value === "string" ? props.value : "";
  const storedName = useFormValue(["personName"]) as string | undefined;
  const { people, error, unavailable } = useDirectoryPeople(
    personId ? [personId] : [],
  );
  const person = personId ? people.get(personId) : undefined;
  const drifted = person && storedName && person.name !== storedName;

  return (
    <Stack space={2}>
      {props.renderDefault(props)}
      {personId && (
        <Card
          padding={2}
          radius={2}
          tone={person ? "positive" : unavailable ? "caution" : "transparent"}
          border
        >
          <Flex align="center" gap={3}>
            {person ? (
              <>
                <Avatar src={person.image} size={1} />
                <Stack space={2} flex={1}>
                  <Text size={1} weight="semibold">
                    {person.name}
                  </Text>
                  <Text size={0} muted>
                    Live from Sanity Home
                  </Text>
                </Stack>
                {drifted && (
                  <Badge tone="caution">name drifted: “{storedName}”</Badge>
                )}
              </>
            ) : (
              <Text size={1} muted>
                {unavailable ? error : "No person with this id in Sanity Home"}
              </Text>
            )}
          </Flex>
        </Card>
      )}
    </Stack>
  );
}
