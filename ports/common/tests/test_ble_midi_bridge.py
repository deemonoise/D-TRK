from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, call, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ble_midi_bridge as bridge


class MidiBridgeTests(unittest.TestCase):
    def test_discovery_lists_only_midi_peers_on_the_powered_adapter(self):
        def device(address, alias, uuids):
            return {"org.bluez.Device1": {"Address": address, "Alias": alias, "UUIDs": uuids}}

        objects = {
            "/org/bluez/hci0/dev_good": device("A4:C1:38:97:AC:DC", "MIDI PAD-01", [bridge.MIDI_UUID]),
            "/org/bluez/hci0/dev_audio": device("10:20:30:40:50:60", "Audio", []),
            "/org/bluez/hci0/dev_invalid": device("invalid", "MIDI", [bridge.MIDI_UUID]),
            "/org/bluez/hci1/dev_other": device("10:20:30:40:50:62", "Other", [bridge.MIDI_UUID]),
        }
        self.assertEqual(bridge.midi_devices(objects, "/org/bluez/hci0"),
                         {"a4:c1:38:97:ac:dc": ("/org/bluez/hci0/dev_good", "MIDI PAD-01")})

    def test_routes_follow_the_selected_peer_and_exact_alsa_port(self):
        def obj(id, **props):
            return {"id": id, "info": {"props": props}}

        address = "a4:c1:38:97:ac:dc"
        nodes = [
            obj(10, **{"node.name": "bluez_midi.A4_C1_38_97_AC_DC", "api.bluez5.role": "client"}),
            obj(11, **{"node.name": "bluez_midi.server", "api.bluez5.role": "server"}),
            obj(12, **{"node.name": "bluez_midi.A4_C1_38_97_AC_DE", "api.bluez5.role": "client"}),
            obj(20, **{"node.id": 10, "port.direction": "out"}),
            obj(21, **{"node.id": 10, "port.direction": "in"}),
            obj(23, **{"node.id": 11, "port.direction": "out"}),
            obj(30, **{"object.path": "alsa:seq:default:client_128:playback_1", "port.direction": "in"}),
            obj(31, **{"object.path": "alsa:seq:default:client_128:capture_2", "port.direction": "out"}),
            obj(32, **{"object.path": "alsa:seq:default:client_129:playback_1", "port.direction": "in"}),
        ]
        self.assertEqual(bridge.links_for(nodes, 128, address, 1, address, 2), {(20, 30), (31, 21)})
        self.assertEqual(bridge.links_for(nodes, 128, address, 1, "", -1), {(20, 30)})
        self.assertEqual(bridge.links_for(nodes, 128, "00:00:00:00:00:00", 1, "", -1), set())
        self.assertEqual(bridge.links_for(nodes[1:], 128, address, 1, address, 2), set())

    def test_bridge_only_connects_the_selected_midi_device(self):
        selected = "a4:c1:38:97:ac:dc"
        path = "/org/bluez/hci0/dev_selected"
        manager = Mock()
        manager.GetManagedObjects.return_value = {
            "/org/bluez/hci0": {bridge.ADAPTER: {"Powered": True}},
            path: {bridge.DEVICE: {"Address": selected, "UUIDs": [bridge.MIDI_UUID]}},
            "/org/bluez/hci0/dev_other": {
                bridge.DEVICE: {"Address": "10:20:30:40:50:60", "UUIDs": [bridge.MIDI_UUID]},
            },
        }
        dbus, bus = Mock(), Mock()
        with patch.object(bridge.os, "getppid", side_effect=[42, 42, 1]), \
             patch.object(bridge.signal, "signal"), patch.object(bridge.time, "sleep"):
            bridge.run_bridge(dbus, bus, manager, 128, selected, 1, "", -1)
        bus.get_object.assert_called_once_with(bridge.BLUEZ, path)
        self.assertEqual(dbus.Interface.return_value.mock_calls, [call.Connect(timeout=3)])

    def test_link_reconciliation_restores_its_route_without_removing_foreign_links(self):
        existing = [{"type": "PipeWire:Interface:Link", "info": {"output-port-id": 7, "input-port-id": 8}}]
        owned = {(20, 30)}
        with patch.object(bridge, "change_link", return_value=True) as change:
            bridge.sync_links(existing, {(21, 30)}, owned)
            self.assertEqual(owned, {(21, 30)})
            change.assert_called_once_with((21, 30))


if __name__ == "__main__":
    unittest.main()
