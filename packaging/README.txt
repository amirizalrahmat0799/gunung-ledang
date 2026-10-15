GUNUNG LEDANG
A co-op horror Metroidvania for 1 to 4 players.
https://github.com/amirizalrahmat0799/gunung-ledang

PLAY
  Run gunung_ledang (gunung_ledang.exe on Windows).
  Press 1 to host a game, or 2 to join one by typing the host's IP address.
  The host may be asked to allow the game through the firewall: allow it
  (it uses UDP port 27015). Friends over the internet need that port forwarded
  to the host's PC.

  Windows may show "Windows protected your PC" because the game isn't signed.
  Click "More info", then "Run anyway".

CONTROLS            Keyboard               Gamepad
  Move              A / D or arrows        Left stick, d-pad
  Jump              Space, Z or K          A
  Attack (keris)    J or X                 X
  Dash              Shift, C or L          B or RB
  Drop through      S or Down              Down
  Leave the game    Esc

NETCODE DEMO
  F1  network overlay       F2  simulate lag and packet loss
  F3  prediction on/off     F4  smoothing on/off      F5  ghost boxes

ALSO INCLUDED
  ledang_server   dedicated server:  ledang_server --port 27015
  ledang_bot      test players:      ledang_bot --connect <host> --bots 3

  Fill your own game with bots:      gunung_ledang --host --bots 3

Apache License 2.0. Copyright (c) Ahmad Amirizal Rahmat.
