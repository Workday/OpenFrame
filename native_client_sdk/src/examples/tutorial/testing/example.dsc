{
  'TOOLS': ['glibc', 'newlib', 'pnacl'],
  'SEL_LDR': True,
  'TARGETS': [
    {
      'NAME' : 'testing',
      'TYPE' : 'main',
      'SOURCES' : ['testing.cc'],
      'LIBS' : ['ppapi_simple', 'nacl_io', 'ppapi_cpp', 'ppapi', 'gtest', 'pthread'],
      'CXXFLAGS': ['-Wno-sign-compare', '-Wno-unused-private-field'],
      'CFLAGS_GCC': ['-Wno-unused-local-typedefs'],
    }
  ],
  'DATA': [
    'example.js'
  ],
  'DEST': 'examples/tutorial',
  'NAME': 'testing',
  'TITLE': 'Testing with gtest',
  'GROUP': 'Tutorial'
}

