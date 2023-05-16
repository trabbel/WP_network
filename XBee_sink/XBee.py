import time
import serial
from typing import Tuple


START_DELIMITER = 0x7e
ESCAPE_CHAR = 0x7d


def writeFrame(frame: bytearray, addr16: int, addr64: int, payload: bytearray, payloadSize: int) -> int:
    # Define a temporary array of sufficient size for everything that may be escaped
    tempFrame = bytearray(17 + payloadSize)
    # Frame delimiter
    frame[0] = START_DELIMITER
    # Payload length starting with frame type byte, excluding checksum and necessary escape characters
    frame_len = 0x0E + payloadSize
    tempFrame[0] = (frame_len >> 8) & 0xFF
    tempFrame[1] = frame_len & 0xFF
    # API ID (TX request)
    tempFrame[2] = 0x10
    # Frame ID
    tempFrame[3] = 0x01
    # 64-bit destination address
    for i in range(8):
        tempFrame[4+i] = (addr64 >> (7-i)*8) & 0xFF
    # 16-bit address
    tempFrame[12] = (addr16 >> 8) & 0xFF
    tempFrame[13] = (addr16 >> 0) & 0xFF
    # Broadcast radius
    tempFrame[14] = 0x00
    # Options
    tempFrame[15] = 0x00
    # Payload
    tempFrame[16:16+payloadSize] = payload[:]
    checksum = sum(tempFrame[i] for i in range(2, 16+payloadSize))
    tempFrame[16+payloadSize] = 0xFF - (checksum%256)
    # Escape the payload and count how many additional escape characters have to be transmitted
    escapes = escapePayload(tempFrame, frame, payloadSize)
    # The frame is now written in the buffer and ready to send
    return 18 + payloadSize + escapes


def escapePayload(payload: bytearray, tx_buf: bytearray, payloadSize: int) -> int:
    # Counter for needed escape characters
    escapes = 0
    # Where to start writing the payload in the buffer
    pos = 1
    # Go through each payload byte
    for i in range(17 + payloadSize):
        # Check if the current byte has to be escaped
        # 0x7e         | frame delimiter, signifies a new frame
        # 0x7d         | escape character, signifies that the next character is escaped
        # 0x11, 0x13   | software control character
        if payload[i] in [0x7E, 0x7D, 0x11, 0x13]:
            # If a byte has to be escaped, first write 0x7d, followed by the byte XORed with 0x20
            tx_buf[pos] = 0x7D
            tx_buf[pos+1] = payload[i] ^ 0x20
            pos += 2
            escapes += 1
        else:
            # Otherwise write the byte
            tx_buf[pos] = payload[i]
            pos += 1
    return escapes


def readFrame(serial_port: serial.Serial, frame: bytearray) -> Tuple[int, int]:
    result = (0x00, 0)
    length_bytes = bytearray(2)

    # Read the length bytes. Return an error if no data is available or
    # another frame delimiter is encountered
    for i in range(2):
        byte = waitForByte(serial_port)
        if byte is None:
            return result

        length_bytes[i] = byte

        if length_bytes[i] == START_DELIMITER:
            return -1, 0
        elif length_bytes[i] == ESCAPE_CHAR:
            # If an escape char is encountered, read the next byte instead and unescape it
            byte = waitForByte(serial_port)
            if byte is None:
                return result

            length_bytes[i] = byte
            length_bytes[i] ^= 0x20

    # The length defined by the length bytes is not equal to the actual amount
    # of bytes, escape characters are not counted
    payload_size = (length_bytes[0] << 8) + length_bytes[1]

    # Read the payload. Return an error if no data is available or
    # another frame delimiter is encountered
    for i in range(payload_size + 1):
        byte = waitForByte(serial_port)
        if byte is None:
            return result

        frame[i] = byte

        if frame[i] == START_DELIMITER:
            return -1, 0
        elif frame[i] == ESCAPE_CHAR:
            # If an escape char is encountered, read the next byte instead and unescape it
            byte = waitForByte(serial_port)
            if byte is None:
                return result

            frame[i] = byte
            frame[i] ^= 0x20

    checksum = sum(frame[i] for i in range(payload_size))
    # Return an error if the checksum is not matching
    if frame[payload_size] != 0xFF - (checksum%256):
        return -2, 0
    return frame[0], payload_size

def waitForByte(serial_port: serial.Serial, timeout_cnt: int = 1000) -> int:
    timeout = 0
    while serial_port.in_waiting < 1:
        timeout += 1
        if timeout > timeout_cnt:
            return None
        time.sleep(0.000008)  # Wait for 8 microseconds
    return serial_port.read()[0]