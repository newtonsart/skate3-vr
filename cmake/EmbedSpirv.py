from pathlib import Path
import struct
import sys

data=Path(sys.argv[1]).read_bytes()
if len(data)%4 or len(data)<20 or data[:4]!=b'\x03\x02\x23\x07':
    raise ValueError('Invalid SPIR-V module')
words=struct.unpack('<'+'I'*(len(data)//4),data)
text='#pragma once\n#include <cstdint>\ninline constexpr uint32_t kVrColorSpirv[] = {\n'
text+=''.join(','.join(f'0x{x:08x}' for x in words[i:i+8])+',\n' for i in range(0,len(words),8))
Path(sys.argv[2]).write_text(text+'};\n',encoding='utf-8')
