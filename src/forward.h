#pragma once

// Export forwarding.
//
// The game (NSUNSC.exe) imports D3DCOMPILER_47.dll from its own directory, so the
// proxy has to expose the exact same export surface. Instead of writing stub
// wrappers, every export is emitted as a linker *forwarder* that points at the
// renamed original, d3dcompiler_47_o.dll, which must sit next to the executable.
//
// Export list generated from the PE export directory of the real
// d3dcompiler_47.dll / d3dcompiler_47_o.dll:
//   29 named exports, ordinals 1..29, contiguous, no gaps.
#if defined(_M_X64)

#pragma comment(linker, "/export:D3DAssemble=d3dcompiler_47_o.D3DAssemble,@1")
#pragma comment(linker, "/export:DebugSetMute=d3dcompiler_47_o.DebugSetMute,@2")
#pragma comment(linker, "/export:D3DCompile=d3dcompiler_47_o.D3DCompile,@3")
#pragma comment(linker, "/export:D3DCompile2=d3dcompiler_47_o.D3DCompile2,@4")
#pragma comment(linker, "/export:D3DCompileFromFile=d3dcompiler_47_o.D3DCompileFromFile,@5")
#pragma comment(linker, "/export:D3DCompressShaders=d3dcompiler_47_o.D3DCompressShaders,@6")
#pragma comment(linker, "/export:D3DCreateBlob=d3dcompiler_47_o.D3DCreateBlob,@7")
#pragma comment(linker, "/export:D3DCreateFunctionLinkingGraph=d3dcompiler_47_o.D3DCreateFunctionLinkingGraph,@8")
#pragma comment(linker, "/export:D3DCreateLinker=d3dcompiler_47_o.D3DCreateLinker,@9")
#pragma comment(linker, "/export:D3DDecompressShaders=d3dcompiler_47_o.D3DDecompressShaders,@10")
#pragma comment(linker, "/export:D3DDisassemble=d3dcompiler_47_o.D3DDisassemble,@11")
#pragma comment(linker, "/export:D3DDisassemble10Effect=d3dcompiler_47_o.D3DDisassemble10Effect,@12")
#pragma comment(linker, "/export:D3DDisassemble11Trace=d3dcompiler_47_o.D3DDisassemble11Trace,@13")
#pragma comment(linker, "/export:D3DDisassembleRegion=d3dcompiler_47_o.D3DDisassembleRegion,@14")
#pragma comment(linker, "/export:D3DGetBlobPart=d3dcompiler_47_o.D3DGetBlobPart,@15")
#pragma comment(linker, "/export:D3DGetDebugInfo=d3dcompiler_47_o.D3DGetDebugInfo,@16")
#pragma comment(linker, "/export:D3DGetInputAndOutputSignatureBlob=d3dcompiler_47_o.D3DGetInputAndOutputSignatureBlob,@17")
#pragma comment(linker, "/export:D3DGetInputSignatureBlob=d3dcompiler_47_o.D3DGetInputSignatureBlob,@18")
#pragma comment(linker, "/export:D3DGetOutputSignatureBlob=d3dcompiler_47_o.D3DGetOutputSignatureBlob,@19")
#pragma comment(linker, "/export:D3DGetTraceInstructionOffsets=d3dcompiler_47_o.D3DGetTraceInstructionOffsets,@20")
#pragma comment(linker, "/export:D3DLoadModule=d3dcompiler_47_o.D3DLoadModule,@21")
#pragma comment(linker, "/export:D3DPreprocess=d3dcompiler_47_o.D3DPreprocess,@22")
#pragma comment(linker, "/export:D3DReadFileToBlob=d3dcompiler_47_o.D3DReadFileToBlob,@23")
#pragma comment(linker, "/export:D3DReflect=d3dcompiler_47_o.D3DReflect,@24")
#pragma comment(linker, "/export:D3DReflectLibrary=d3dcompiler_47_o.D3DReflectLibrary,@25")
#pragma comment(linker, "/export:D3DReturnFailure1=d3dcompiler_47_o.D3DReturnFailure1,@26")
#pragma comment(linker, "/export:D3DSetBlobPart=d3dcompiler_47_o.D3DSetBlobPart,@27")
#pragma comment(linker, "/export:D3DStripShader=d3dcompiler_47_o.D3DStripShader,@28")
#pragma comment(linker, "/export:D3DWriteBlobToFile=d3dcompiler_47_o.D3DWriteBlobToFile,@29")

#endif // _M_X64
