
from .objdump import *
from io import BytesIO

def test_table():
    t = table.load(BytesIO(b'''\
00000000030726f0 0x3084588 _OBJC_CLASS_$_WebCoreAudioInputMuteChangeListener
    isa        0x3084560 _OBJC_METACLASS_$_WebCoreAudioInputMuteChangeListener
    superclass 0x0 _OBJC_CLASS_$_NSObject
    cache      0x0 __objc_empty_cache
    vtable     0x0
    data       0x3073830 __OBJC_CLASS_RO_$_WebCoreAudioInputMuteChangeListener
        flags          0x10
        instanceStart  8
        instanceSize   8
        reserved       0x00000000
        ivarLayout     0x0
        name           0x2d95c20 WebCoreAudioInputMuteChangeListener
        baseMethods    0x2a3b0e8 __OBJC_$_INSTANCE_METHODS_WebCoreAudioInputMuteChangeListener
            entsize 12 (relative)
            count   3
            name    0x643710 (0x307e800)
            types   0x36d58d (0x2da8681) v16@0:8
            imp     0xfd99a9a4 (0x3d5a9c) -[WebCoreAudioInputMuteChangeListener start]
            name    0x6437bc (0x307e8b8)
            types   0x36d581 (0x2da8681) v16@0:8
            imp     0xfd99aa24 (0x3d5b28) -[WebCoreAudioInputMuteChangeListener stop]
            name    0x645220 (0x3080328)
            types   0x36d57d (0x2da8689) v24@0:8@16
            imp     0xfd99aa74 (0x3d5b84) -[WebCoreAudioInputMuteChangeListener handleMuteStatusChangedNotification:]
        baseProtocols  0x0
        ivars          0x0
        weakIvarLayout 0x0
        baseProperties 0x0
'''))
    nt = nested_table.from_parsed(t)
    assert nt.as_tuple() == ((b'isa', b'0x3084560 _OBJC_METACLASS_$_WebCoreAudioInputMuteChangeListener'),
     (b'superclass', b'0x0 _OBJC_CLASS_$_NSObject'),
     (b'cache', b'0x0 __objc_empty_cache'),
     (b'vtable', b'0x0'),
     (b'data',
      ((b'flags', b'0x10'),
       (b'instanceStart', b'8'),
       (b'instanceSize', b'8'),
       (b'reserved', b'0x00000000'),
       (b'ivarLayout', b'0x0'),
       (b'name', b'0x2d95c20 WebCoreAudioInputMuteChangeListener'),
       (b'baseMethods',
        ((b'entsize', b'12 (relative)'),
         (b'count', b'3'),
         (b'name', b'0x643710 (0x307e800)'),
         (b'types', b'0x36d58d (0x2da8681) v16@0:8'),
         (b'imp',
          b'0xfd99a9a4 (0x3d5a9c) -[WebCoreAudioInputMuteChangeListener start]'),
         (b'name', b'0x6437bc (0x307e8b8)'),
         (b'types', b'0x36d581 (0x2da8681) v16@0:8'),
         (b'imp',
          b'0xfd99aa24 (0x3d5b28) -[WebCoreAudioInputMuteChangeListener stop]'),
         (b'name', b'0x645220 (0x3080328)'),
         (b'types', b'0x36d57d (0x2da8689) v24@0:8@16'),
         (b'imp',
          b'0xfd99aa74 (0x3d5b84) -[WebCoreAudioInputMuteChangeListener handleMu'
          b'teStatusChangedNotification:]'))),
       (b'baseProtocols', b'0x0'),
       (b'ivars', b'0x0'),
       (b'weakIvarLayout', b'0x0'),
       (b'baseProperties', b'0x0'))))


