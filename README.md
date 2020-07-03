# noRSI #

noRSI is an activity-tracking server for Wayland. Its primary focus is to track
user activity for the prevention of repetitive strain injuries.

It uses KDE's idle protocol to be able to alert the user when they have been
active for some period of time without taking a break. Please note: as of the
initial release, it's clear that there are some limitations on accuracy (see
"Limitations" below). Hopefully KDE's protocol can be extended, or a new one can
be created to meet the needs of those who need help preventing physical injury
from extended computer use.

## Build ##

```
$ meson build
$ cd build
$ ninja
```

## Run ##

```
$ cd build
$ ./norsi
```

## How does it work? ##

A unix domain socket will be created at `$XDG_RUNTIME_DIR/norsi/socket.sock`.

You can connect to this and send commands as newline-terminated strings. You get
back a JSON object, e.g.:

```
$ echo "status" | nc -W1 -U $XDG_RUNTIME_DIR/norsi/socket.sock | jq .
{
  "periods": [
    {
      "name": "micro",
      "safe": false,
      "accumulated_seconds": 681,
      "break_at": 180
    },
    {
      "name": "normal",
      "safe": false,
      "accumulated_seconds": 3349,
      "break_at": 2700
    },
    {
      "name": "workday",
      "safe": true,
      "accumulated_seconds": 6782,
      "break_at": 14400
    }
  ]
}
```

You can pass that into whatever sort of script/tool you choose to implement
tracking/alerts in a way that works for you.

## Planned Features ##

*   Configurable activity/break periods (coming soon)
*   Historical reporting

## Limitations ##

*   If you hold down a key, there's no way to see that as activity. Wayland
    requires clients to implement their own key-repeat functionality, so there's
    no easy way to record these "virtual" keystrokes from the idle manager.
*   If you have open an application which inhibits idleness (e.g. media players)
    then it will count as activity. There's no way to distinguish between
    keyboard/mouse activity and an application asking the compositor to pretend
    that there's keyboard/mouse activity. You might consider this valid given
    that watching video can cause eye strain, but it wouldn't be valid if you
    were simply listening to a video without actively watching it.

Some aspects of tracking activity/idleness are not straightforward, but noRSI
aims to be as helpful as it can manage.
