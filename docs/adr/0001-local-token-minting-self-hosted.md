# Local token minting for a self-hosted, single-operator client

The Client mints Agora tokens locally from the App Certificate rather than fetching
them from a token server. This is normally unsafe — the App Certificate is the master
secret for the whole Agora project — but it is acceptable here because the Client is
strictly self-hosted: a single Operator runs it on their own machine, supplies their
own certificate at runtime via environment variable, and never distributes a build to
third parties. The certificate is never baked into the binary or the release archive.

We chose this over a token-endpoint design because it keeps the tool zero-infrastructure
for its only audience (the Operator). If the Client ever needs to be handed to other
people, this decision must be revisited — distribution and local minting are mutually
exclusive.
