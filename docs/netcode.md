# Netcode walkthrough

Gunung Ledang uses the model Valve described for Half-Life and the Source engine
([Source Multiplayer Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking),
Yahn Bernier's *Latency Compensating Methods*). This page walks through each part, where it lives in the code,
and the choices made for a platformer, which is less forgiving than a shooter: a jump that lands 3 px off is
visible.

## 1. One simulation, two machines

`step_player(state, buttons, map)` (`src/sim/player.cpp`) is a pure function on integers. Positions and
velocities are Q8 fixed point (1/256 px), and there are no floats, no clocks and no randomness. The server and
every client run exactly this function, so the same commands from the same state give the same result on any
compiler or CPU. Prediction is only exact because of this; the `player_is_deterministic` test replays 3000
random inputs from a midpoint and compares every byte.

Everything that is not the local player's movement (spirits, damage, pickups) runs only on the server.

## 2. User commands

Every client tick (60 Hz) the client samples the buttons into a `UserCmd`:

| field | |
|---|---|
| `number` | increases by one per tick |
| `buttons` | 7 bits |
| `view_tick_q8` | the server tick, times 256, that the client was drawing other entities at |

Each packet carries the newest **8** commands (`encode_commands`: the first in full, then 7 bits of buttons and a
varint delta of `view_tick` per command, about 25 bytes for all 8). A command is only lost if 8 consecutive
packets are, so loss turns into duplicates instead of missed inputs.

The server (`Server::handle_packet`) inserts commands into a per-client queue sorted by number, dropping any it
has already run or queued, so reordered packets don't lose commands either. Each tick it runs the queue
(`run_commands`):

- at most `kMaxCmdsPerTick` (4) per tick, so a client that stalled catches up quickly;
- a budget that refills by one command per tick, so over time nobody gets more than 60 commands a second
  (sending commands faster would otherwise be a speed hack);
- the queue is capped at 12; a client whose clock runs fast loses its oldest commands instead of building latency.

Gaps in the numbering (all 8 copies lost) are counted in `ServerStats::cmd_gaps`; the soak test requires fewer
than 1 per 1000 commands, and in practice sees 0.

## 3. Snapshots and delta compression

Every 2 ticks the server captures a `Snapshot` (`capture` in `src/proto/protocol.cpp`): a table of up to 64
entities, each a type plus up to 20 int32 fields. Entity 0 is the world (shared abilities, lit lanterns,
victory), 1 to 4 are players, and 5 onward are spirits. `last_cmd`, the newest command the server has run for
*this* client, is included so the client knows where to replay from.

The snapshot is encoded against a **baseline**: the newest snapshot this client is *known* to have. The client
never sends "I got snapshot N"; instead, every server packet carries its snapshot's tick as a tag, and when the
client's packet header acks that packet (sequence number plus a 32-bit ack field), the connection reports the tag
back (`Connection::take_acked_tags`). Since the baseline was acked, the client has it.

Encoding (`encode_snapshot`), per entity that differs from the baseline:

```
1 | entity id (6 bits) | op (2 bits: update, create, remove)
update: for each field: changed? (1 bit) [zigzag varint of the difference]
create: type (2 bits), then each field as a varint
```

Varints cost 2 bits for 0, 7 for values within ±15, 14 for ±2047 and 34 otherwise, so a running player's x
(+1152 over two ticks) costs 15 bits with its change bit, and a spirit sleeping out of sight costs nothing. A
full snapshot of the world is about 165 bytes; a typical delta is about 90, and nothing changing costs 11 bytes. Server and client both keep the last
32 snapshots (about one second); a baseline older than that is not used, and the server sends a full snapshot
instead.

## 4. Reliability on top of UDP

`Connection` (`src/net/connection.cpp`) does what TCP would do, but without blocking the newest data behind lost
old data:

- every packet has a 16-bit sequence number, plus `ack` (newest received) and `ack_bits` (which of the 32 before
  it arrived), so each ack is repeated in up to 33 packets;
- acked packets give the RTT (smoothed) and the loss over a recent window;
- reliable messages (welcome, game events) ride in ordinary packets. They are resent after
  max(100 ms, 1.25 x RTT) until a packet containing them is acked, and the receiver releases them in order,
  exactly once.

The test `connection_reliable_in_order_over_bad_link` sends 500 messages over a link that drops 20% and
duplicates and reorders packets.

## 5. Client-side prediction and reconciliation

The client applies each command to its own player immediately (`ClientCore::client_tick`), so movement has no
delay. When a newer snapshot arrives (`reconcile`):

1. take the server's state of our player, which reflects every command up to `last_cmd`;
2. re-run our stored commands after `last_cmd` on top of it;
3. compare with what we had predicted.

Because the simulation is deterministic, the result is normally identical. It differs when the server applied
something the client could not know (a spirit's bite and its knockback, a shrine another player reached first).
Then the client takes the corrected state but keeps drawing the player at the old spot plus an offset that decays
over about 80 ms (`smooth_x_`, `smooth_y_`), so a correction is a slide instead of a jump. Errors over 64 px
(respawns) snap. In the soak test about 1.5% of reconciliations correct anything, and those are hits.

## 6. Interpolation

Other players and spirits are drawn in the past, between two snapshots that have both arrived
(`ClientCore::update_views`), so they move smoothly even though snapshots come at 30 Hz, late, or not at all.

- The client keeps an estimate of the server's current tick that advances in real time and is nudged 10%
  toward each new snapshot's tick, so network jitter doesn't make it jump.
- It renders at that estimate minus a delay: 4 ticks (two snapshots), plus 2.5 times the measured arrival
  jitter, plus 2 ticks when packets are being lost. The delay changes by at most 2 ticks per second, so time never
  visibly jumps. With 150 ms and 5% loss it settles around 7 to 8 ticks (about 125 ms).
- An entity that moved more than 3 tiles between snapshots (respawn) is not interpolated.
- If the newest snapshot is older than the render time, the client holds instead of extrapolating
  (`starved_frames`); a ghost that freezes for a frame is less wrong than one that flies through a wall.

## 7. Lag compensation

Interpolation means a player sees spirits where they were about 125 ms plus half the ping ago. Without
compensation, a swing that clearly lands on screen would miss on the server. So:

- the server keeps every spirit's state for the last 128 ticks (`World::record_history`);
- each command carries `view_tick_q8`, the fractional tick the client was rendering;
- on the tick a swing connects, the server tests the blade against each spirit's box **as it was at that tick**,
  interpolated between the two neighbouring history entries exactly as the client did
  (`World::enemy_box_in_past`), and applies the hit to the spirit as it is now.

The rewind is clamped to 30 ticks (500 ms), so a client can't claim it saw the world long ago. Spirits are
only rewound for the player's own attacks; damage the spirits deal is decided in the present, which favours the
player under lag, as in most action games.

## 8. Connecting

1. Client sends `ConnectRequest` with a random 64-bit salt, padded to 1000 bytes.
2. Server replies with a `Challenge` echoing the client salt and adding its own (smaller than the request, so a
   spoofed source address can't be used for amplification).
3. Client replies with `ChallengeResponse` (client salt XOR server salt), also padded.
4. Server assigns a slot and sends a reliable `Welcome`. Payload packets then flow both ways.

`Disconnect` must carry the combined salt. Five seconds of silence times a client out.

## 9. Testing it without a bad network

`LinkConditioner` (`src/net/link_conditioner.cpp`) sits between the game and the socket and delays, jitters,
drops and duplicates outgoing packets. It is used by:

- **F2** in the game, to feel the difference;
- `ledang_server --lag 75 --jitter 10 --loss 0.05` and the same flags on `ledang_bot`;
- the soak test (`tests/test_netgame.cpp`), which runs a server and three bots over real UDP sockets on loopback in
  fast virtual time: two minutes of play in under two seconds.
