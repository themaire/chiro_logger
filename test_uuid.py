#!/usr/bin/env python3
"""
Test script pour vérifier la conversion UUID
"""

def uuid_string_to_bin(uuid_str):
    """Convertir un UUID string en bytes comme dans le code C"""
    # Format: "12345678-1234-1234-1234-123456789ABC"
    #          0        1    2    3    4
    #          01234567890123456789012345678901234567
    
    uuid_bin = [0] * 16
    
    # Partie 1: 12345678 (4 bytes) -> positions 12-15 (reversed)
    uuid_bin[15] = int(uuid_str[0:2], 16)
    uuid_bin[14] = int(uuid_str[2:4], 16)
    uuid_bin[13] = int(uuid_str[4:6], 16)
    uuid_bin[12] = int(uuid_str[6:8], 16)
    
    # Partie 2: 1234 (2 bytes) -> positions 10-11 (reversed)
    uuid_bin[11] = int(uuid_str[9:11], 16)
    uuid_bin[10] = int(uuid_str[11:13], 16)
    
    # Partie 3: 1234 (2 bytes) -> positions 8-9 (reversed)
    uuid_bin[9] = int(uuid_str[14:16], 16)
    uuid_bin[8] = int(uuid_str[16:18], 16)
    
    # Partie 4: 1234 (2 bytes) -> positions 6-7 (as is)
    uuid_bin[7] = int(uuid_str[19:21], 16)
    uuid_bin[6] = int(uuid_str[21:23], 16)
    
    # Partie 5: 123456789ABC (6 bytes) -> positions 0-5 (reversed)
    uuid_bin[5] = int(uuid_str[24:26], 16)
    uuid_bin[4] = int(uuid_str[26:28], 16)
    uuid_bin[3] = int(uuid_str[28:30], 16)
    uuid_bin[2] = int(uuid_str[30:32], 16)
    uuid_bin[1] = int(uuid_str[32:34], 16)
    uuid_bin[0] = int(uuid_str[34:36], 16)
    
    return uuid_bin

def main():
    service_uuid = "12345678-1234-1234-1234-123456789ABC"
    char_uuid = "87654321-4321-4321-4321-CBA987654321"
    
    print(f"UUID Service: {service_uuid}")
    service_bin = uuid_string_to_bin(service_uuid)
    print(f"Binaire: {' '.join(f'{b:02x}' for b in service_bin)}")
    
    # Reconstituer pour vérifier
    reconstructed = f"{service_bin[15]:02x}{service_bin[14]:02x}{service_bin[13]:02x}{service_bin[12]:02x}-{service_bin[11]:02x}{service_bin[10]:02x}-{service_bin[9]:02x}{service_bin[8]:02x}-{service_bin[7]:02x}{service_bin[6]:02x}-{service_bin[5]:02x}{service_bin[4]:02x}{service_bin[3]:02x}{service_bin[2]:02x}{service_bin[1]:02x}{service_bin[0]:02x}"
    print(f"Reconstruit: {reconstructed.upper()}")
    
    print("\n" + "="*50)
    
    print(f"UUID Caractéristique: {char_uuid}")
    char_bin = uuid_string_to_bin(char_uuid)
    print(f"Binaire: {' '.join(f'{b:02x}' for b in char_bin)}")
    
    # Reconstituer pour vérifier
    reconstructed = f"{char_bin[15]:02x}{char_bin[14]:02x}{char_bin[13]:02x}{char_bin[12]:02x}-{char_bin[11]:02x}{char_bin[10]:02x}-{char_bin[9]:02x}{char_bin[8]:02x}-{char_bin[7]:02x}{char_bin[6]:02x}-{char_bin[5]:02x}{char_bin[4]:02x}{char_bin[3]:02x}{char_bin[2]:02x}{char_bin[1]:02x}{char_bin[0]:02x}"
    print(f"Reconstruit: {reconstructed.upper()}")

if __name__ == "__main__":
    main()
