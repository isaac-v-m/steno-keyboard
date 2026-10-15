import serial
LEFT_KEYS = {
    "L00": ("s",  "z"),
    "L01": ("h",  "h"),
    "L02": ("p",  "b"),
    "L03": ("ʃ",  "ʒ"),
    "L04": ("j",  "w"),
    "L05": ("f",  "v"),
    "L06": ("k",  "g"),
    "L07": ("t",  "d"),
    "L08": ("r",  "l"),
    "L09": ("n",  "m"),
}
THUMB_KEYS = {
    "L10": ("a",  "a"),
    "L11": ("o",  "o"),
    "L12": (":",  ":"),
    "R10": ("e",  "ʌ"),
    "R11": ("I",  "i"),
    "R12": ("ʊ",  "u"),
}
RIGHT_KEYS = {
    "R00": ("j",  "w"),
    "R01": ("n",  "m"),
    "R02": ("k",  "g"),
    "R03": ("h",  "h"),
    "R04": ("t",  "d"),
    "R05": ("ng", "ng"),
    "R06": ("r",  "l"),
    "R07": ("p",  "b"),
    "R08": ("f",  "v"),
    "R09": ("s",  "z"),
}
# Packet: 0xFF start byte, then 5 data bytes using only bits 0-6, so no data
# byte can ever equal 0xFF. Bytes 0-3 hold the keys in key order (bit n = the
# nth key listed below); byte 4 holds the layer flags. The firmware maps its
# matrix positions onto these bits, so the matrix wiring doesn't matter here.
PACKET_LEN = 5
PACKET_KEYS = [
    ["L00", "L01", "L02", "L03", "L04", "L05", "L06"],  # byte0
    ["L07", "L08", "L09", "L10", "L11", "L12"],         # byte1
    ["R00", "R01", "R02", "R03", "R04", "R05", "R06"],  # byte2
    ["R07", "R08", "R09", "R10", "R11", "R12"],         # byte3
]
LEFT_LAYER_BIT, THUMB_LAYER_BIT, RIGHT_LAYER_BIT = 0, 1, 2  # in byte4
def decode_packet(data: bytes ) -> dict:
    """
    Decode a 5-byte payload (after the 0xFF start byte) into a chord dict.
    """

    if len(data) != PACKET_LEN:
        raise ValueError(f"Did not recive {PACKET_LEN} bytes")
    flags = data[4]
    l_is_layer2 = bool(flags & (1 << LEFT_LAYER_BIT))
    thumb_is_layer2 = bool(flags & (1 << THUMB_LAYER_BIT))
    r_is_layer2 = bool(flags & (1 << RIGHT_LAYER_BIT))

    pressed = {
        name
        for key_byte, names in zip(data, PACKET_KEYS)
        for bit, name in enumerate(names)
        if key_byte & (1 << bit)
    }
    def keys(table, is_layer2):
        return [table[name][1 if is_layer2 else 0] for name in sorted(table) if name in pressed]

    return {
        "left_layer": l_is_layer2,
        "thumb_layer": thumb_is_layer2,
        "right_layer": r_is_layer2,
        "left_keys": keys(LEFT_KEYS, l_is_layer2),
        "thumb_keys": keys(THUMB_KEYS, thumb_is_layer2),
        "right_keys": keys(RIGHT_KEYS, r_is_layer2),
    }
def read_chords(port="COM3"):
    # Pro Micro uses native USB serial: the baud rate is ignored, and the
    # COM port number can differ from the one an FTDI/CH340 board gets.
    with serial.Serial(port, 9600, timeout=1) as ser:
        buf = bytearray()
        while True:
            byte = ser.read(1)
            if not byte:
                continue
            byte_0 = byte[0]
            if byte_0 == 0xFF:
                buf = bytearray()
            elif byte_0 & 0x80:
                # Not a valid data byte: drop the partial packet and resync.
                buf = bytearray()
            else:
                buf.append(byte_0)
                if len(buf) == PACKET_LEN:
                    yield decode_packet(bytes(buf))
                    buf = bytearray()
if __name__ == "__main__":
    import sys
    for chord in read_chords(*sys.argv[1:2]):
        print(chord)
