tupperware
=======

Monitor and keep up a VPN link the other side wishes to time out.

This lightweight link monitor is used to keep a VPN link open (via a tun/tap) name even when idle.

Many vpn providers might drop a link that is idle by disconnecting it and the user may need to re-authorize.

In this case, my clients re-authorization procedure was cumbersome, requiring a multi-factor authentication step (an app authorization). This prevented other users who shared the link from using the link if it timed out.

# Purpose

Many vpn providers might drop a link that is idle by disconnecting it and the user may need to re-authorize.

This keeps the link open by periodically sending ping packets to a destination known to exist on the other side.

The client was very happy with this as its essentially a fire-and-forget solution that you really dont remember exists once deployed.

Requirements
------------

This is written with libev.

# Implementation

This is entirely an event driven program. The VPN device being monitored is completely separate from the program used to create it.

The user configures an config file with the device name listed.

The program uses AF_NETLINK monitors to regiser for device add/remove events that match the device name the VPN ends up with.

If the device comes up / exists, the program uses an interval timer to send a ping packet down the device path to keep the link alive.

If the device is deleted the timer is stopped.

One cool little quirk on recent-ish Linux kernels is that ICMP ECHO packets can be sent without CAP_NET_RAW or CAP_NET_ADMIN provided the user is in an acceptable user group allowed to send these packet types. As such the sysctl net.ipv4.ping_group_range should be set to permit this and you can ditch any root requirements to use it.

# Performance

This is a single threaded program that must run in the same network namespace as the device being monitored. Its performance requirements are minimal.
