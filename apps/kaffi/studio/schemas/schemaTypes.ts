import { coffeeEvent } from "./coffeeEvent";
import { firmwareRelease } from "./firmwareRelease";

// People live in the company employee directory (a separate Sanity project),
// pulled live by the device. We only store coffee events here, keyed by the
// directory person _id (coffeeEvent.personId).
export const schemaTypes = [coffeeEvent, firmwareRelease];
