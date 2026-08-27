import { useEffect, useMemo, useState } from "react";
import { useClient } from "sanity";

// Sanity Home — the company employee directory, and the source of truth for
// people. Events only store the directory _id (plus a cosmetic name snapshot);
// anything displayed in this studio should prefer a live directory lookup.
const DIRECTORY_PROJECT_ID = "r3dzy7he";
const DIRECTORY_DATASET = "production";

export const API_VERSION = "2024-01-01";

export type DirectoryPerson = {
  _id: string;
  name: string;
  image?: string;
};

// The studio authenticates with a cookieless session token kept in
// localStorage. Sanity sessions are user-scoped, not project-scoped, so the
// same token authorizes directory reads — provided the viewer is a member of
// the directory project.
function readStudioToken(projectId: string): string | undefined {
  try {
    const raw = localStorage.getItem(`__studio_auth_token_${projectId}`);
    if (!raw) return undefined;
    const token = JSON.parse(raw)?.token;
    return typeof token === "string" ? token : undefined;
  } catch {
    return undefined;
  }
}

export function useDirectoryClient() {
  const client = useClient({ apiVersion: API_VERSION });
  return useMemo(() => {
    const token = readStudioToken(client.config().projectId ?? "");
    return client.withConfig({
      projectId: DIRECTORY_PROJECT_ID,
      dataset: DIRECTORY_DATASET,
      useCdn: false,
      ignoreBrowserTokenWarning: true,
      // Fall back to cookie auth if the token isn't where we expect it
      ...(token ? { token } : { withCredentials: true }),
    });
  }, [client]);
}

// Live-resolve directory people by _id. Degrades gracefully: if Sanity Home
// is unreachable (no access / missing CORS origin), reports why and callers
// fall back to the stored name snapshots.
export function useDirectoryPeople(ids: string[]) {
  const directory = useDirectoryClient();
  const [people, setPeople] = useState<Map<string, DirectoryPerson>>(new Map());
  const [error, setError] = useState<string | null>(null);
  const key = ids.slice().sort().join(",");

  useEffect(() => {
    if (!key) return;
    let cancelled = false;
    directory
      .fetch<DirectoryPerson[]>(
        '*[_type == "person" && _id in $ids]{_id, name, "image": photo.asset->url + "?w=72&h=72&fit=crop&fm=jpg"}',
        { ids: key.split(",") },
      )
      .then((result) => {
        if (cancelled) return;
        setPeople(new Map(result.map((p) => [p._id, p])));
        setError(null);
      })
      .catch((err: { statusCode?: number; message?: string }) => {
        if (cancelled) return;
        setError(
          err.statusCode
            ? `Sanity Home said ${err.statusCode} — do you have access to ${DIRECTORY_PROJECT_ID}?`
            : `Sanity Home unreachable — add https://kaffi.sanity.studio as a CORS origin on ${DIRECTORY_PROJECT_ID}`,
        );
      });
    return () => {
      cancelled = true;
    };
  }, [directory, key]);

  return { people, error, unavailable: error !== null };
}
