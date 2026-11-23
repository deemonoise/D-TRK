#!/usr/bin/env python3
"""Connect selected BlueZ MIDI devices and link them to the tracker's private ALSA ports."""

import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

MIDI_UUID = "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
BLUEZ = "org.bluez"
ADAPTER = BLUEZ + ".Adapter1"
DEVICE = BLUEZ + ".Device1"
ADDRESS = re.compile(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", re.IGNORECASE)


def midi_devices(objects, adapter):
    devices = {}
    for path, interfaces in objects.items():
        device = interfaces.get(DEVICE, {})
        address = device.get("Address", "").lower()
        uuids = {uuid.lower() for uuid in device.get("UUIDs", ())}
        if path.startswith(adapter + "/") and ADDRESS.fullmatch(address) and MIDI_UUID in uuids:
            name = " ".join((device.get("Alias") or device.get("Name") or address).split())
            devices[address] = (path, name)
    return devices


def links_for(objects, client, in_address, in_port, out_address, out_port):
    props = {item["id"]: (item.get("info") or {}).get("props", {}) for item in objects}
    midi_nodes = {
        p.get("node.name", "").lower(): node for node, p in props.items()
        if p.get("api.bluez5.role") == "client"
    }
    alsa_path = f"alsa:seq:default:client_{client}:"
    port_ids = {(p["node.id"], p.get("port.direction")): port for port, p in props.items()
                if "node.id" in p}
    paths = {p.get("object.path"): port for port, p in props.items()}
    in_node = midi_nodes.get("bluez_midi." + in_address.replace(":", "_"))
    out_node = midi_nodes.get("bluez_midi." + out_address.replace(":", "_"))
    pairs = ((port_ids.get((in_node, "out")), paths.get(alsa_path + f"playback_{in_port}")),
             (paths.get(alsa_path + f"capture_{out_port}"), port_ids.get((out_node, "in"))))
    return {pair for pair in pairs if None not in pair}


def powered_adapter(objects):
    return next((path for path, obj in objects.items() if obj.get(ADAPTER, {}).get("Powered")), None)


def start_discovery(dbus, bus, path):
    adapter = dbus.Interface(bus.get_object(BLUEZ, path), ADAPTER)
    adapter.SetDiscoveryFilter({"Transport": "le", "UUIDs": dbus.Array([MIDI_UUID], signature="s")}, timeout=3)
    adapter.StartDiscovery(timeout=3)
    return adapter


def scan(dbus, bus, manager):
    path = powered_adapter(manager.GetManagedObjects(timeout=2))
    if not path:
        return
    adapter = start_discovery(dbus, bus, path)
    try:
        time.sleep(2.5)
        for address, (_, name) in midi_devices(manager.GetManagedObjects(timeout=2), path).items():
            print(f"{address}\t{name}")
    finally:
        adapter.StopDiscovery(timeout=2)


def pw_links():
    result = subprocess.run(["pw-dump"], check=True, capture_output=True, timeout=2)
    return json.loads(result.stdout)


def change_link(pair, unlink=False):
    args = ["pw-link"] + (["-d"] if unlink else [])
    result = subprocess.run(args + list(map(str, pair)), capture_output=True, timeout=2, check=False)
    return result.returncode == 0


def current_links(graph):
    return {
        (item["info"]["output-port-id"], item["info"]["input-port-id"])
        for item in graph if item.get("type") == "PipeWire:Interface:Link" and item.get("info")
    }


def sync_links(graph, desired, owned):
    present = current_links(graph)
    owned.intersection_update(present)
    for pair in owned - desired:
        if change_link(pair, unlink=True):
            owned.remove(pair)
    for pair in desired - present:
        if change_link(pair):
            owned.add(pair)


class BluezPeers:
    def __init__(self, dbus, bus, manager, addresses):
        self.dbus, self.bus, self.manager = dbus, bus, manager
        self.addresses = addresses
        self.next_attempt = {}
        self.discovery = None
        self.stop_discovery_at = 0.0
        self.next_scan = 0.0

    def stop_discovery(self):
        if self.discovery is not None:
            try:
                self.discovery.StopDiscovery(timeout=2)
            except self.dbus.DBusException:
                pass
            self.discovery = None

    def discover_missing(self, adapter, missing, now):
        if self.discovery and (now >= self.stop_discovery_at or not missing):
            self.stop_discovery()
        if adapter and not self.discovery and missing and now >= self.next_scan:
            self.next_scan = now + 8
            try:
                self.discovery = start_discovery(self.dbus, self.bus, adapter)
                self.stop_discovery_at = now + 3
            except self.dbus.DBusException:
                pass

    def connect_peer(self, address, path, now):
        if now < self.next_attempt.get(address, 0):
            return
        self.next_attempt[address] = now + 4
        device = self.dbus.Interface(self.bus.get_object(BLUEZ, path), DEVICE)
        try:
            device.Connect(timeout=3)
        except self.dbus.DBusException:
            pass

    def update(self, now):
        objects = self.manager.GetManagedObjects(timeout=2)
        adapter = powered_adapter(objects)
        devices = midi_devices(objects, adapter) if adapter else {}
        self.discover_missing(adapter, self.addresses - devices.keys(), now)
        connected = set()
        for address in self.addresses & devices.keys():
            path, _ = devices[address]
            if objects[path][DEVICE].get("Connected"):
                connected.add(address)
            else:
                self.connect_peer(address, path, now)
        return connected


def run_bridge(dbus, bus, manager, client, in_address, in_port, out_address, out_port):
    parent = os.getppid()
    peers = BluezPeers(dbus, bus, manager, {address for address in (in_address, out_address) if address})
    next_graph_check = 0.0
    owned = set()
    previous_connected = set()

    def stop(_signum, _frame):
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, stop)
    try:
        while parent > 1 and os.getppid() == parent:
            now = time.monotonic()
            connected = peers.update(now)
            if (connected or owned) and (connected != previous_connected or now >= next_graph_check):
                try:
                    graph = pw_links()
                    desired = links_for(graph, client, in_address if in_address in connected else "", in_port,
                                        out_address if out_address in connected else "", out_port)
                    sync_links(graph, desired, owned)
                    next_graph_check = now + (5 if owned else 1.5)
                except (OSError, ValueError, subprocess.SubprocessError):
                    pass
                previous_connected = connected
            time.sleep(1.5)
    finally:
        peers.stop_discovery()
        for pair in owned:
            try:
                change_link(pair, unlink=True)
            except (OSError, subprocess.SubprocessError):
                pass


def parse_connection(argv):
    _, client, in_address, in_port, out_address, out_port = argv
    client, in_port, out_port = map(int, (client, in_port, out_port))
    if not 0 <= client < 253 or not (in_address or out_address):
        raise ValueError("Invalid ALSA client or empty MIDI selection")
    for address, port in ((in_address, in_port), (out_address, out_port)):
        if address and (not ADDRESS.fullmatch(address) or not 0 <= port < 255):
            raise ValueError("Invalid MIDI address or ALSA port")
        if not address and port != -1:
            raise ValueError("Unselected MIDI slot has an ALSA port")
    return client, in_address.lower(), in_port, out_address.lower(), out_port


def main(argv):
    if not shutil.which("pw-dump") or not shutil.which("pw-link"):
        return 1
    import dbus

    try:
        bus = dbus.SystemBus()
        manager = dbus.Interface(bus.get_object(BLUEZ, "/"), "org.freedesktop.DBus.ObjectManager")
        if argv == ["scan"]:
            scan(dbus, bus, manager)
            return 0
        if len(argv) == 6 and argv[0] == "connect":
            run_bridge(dbus, bus, manager, *parse_connection(argv))
            return 0
    except (dbus.DBusException, ValueError) as error:
        print(f"BLE MIDI: {error}", file=sys.stderr)
        return 1
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except ImportError:
        sys.exit(1)
