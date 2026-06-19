# Agora Voice Client

A self-hosted command-line program that joins an Agora voice channel and provides
two-way audio, primarily for talking to a remote AI participant.

## Language

**Operator**:
The single trusted person who downloads, configures, and runs the client on their own
machine using their own Agora credentials. There is exactly one per running instance;
the client is not multi-user and is never handed to third parties.
_Avoid_: user, end-user, client (when referring to the person)

**Client**:
The binary itself — the program the Operator runs.
_Avoid_: app, softphone, agent

**Channel**:
The Agora room the Client joins. The AI participant and the Client meet here.
_Avoid_: room, call, session

**App Certificate**:
The Operator's master secret for their Agora project, used to mint tokens locally. It
is supplied at runtime via environment variable and never embedded in a build.
_Avoid_: secret key, API key

**AI Agent**:
The remote participant in the Channel that the Operator talks to. It subscribes to the
Client's audio by the Client UID and publishes its own audio back. It is a separate
system, not part of this Client.
_Avoid_: bot, assistant, remote, AI participant

**Client UID**:
The fixed numeric Agora user id the Operator picks (via `--uid`) for the Client. It is a
coordinated identifier: the AI Agent must be configured to subscribe to this same value,
and the token is minted bound to it. uid `0` (auto-assign) only suits solo testing.
_Avoid_: user id, account, id
