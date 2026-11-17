#![cfg(windows)]
extern crate core;
use winapi::shared::minwindef;
use winapi::shared::minwindef::{BOOL, DWORD, HINSTANCE, LPVOID, UINT, MAX_PATH};
use winapi::shared::d3d9;
use winapi::shared::d3d9::IDirect3D9;
use winapi::um::winnt::LPCSTR;
use winapi::um::libloaderapi::{LoadLibraryA, GetProcAddress, GetModuleFileNameA};
use winapi::um::consoleapi;
use winapi::um::processthreadsapi::CreateThread;
use winapi::um::synchapi::Sleep;
use std::ptr;
use std::ffi::CString;
use std::mem;
use std::os::raw::c_char;

#[link(name = "kernel32")]
extern "system" {
    fn GetSystemDirectoryA(lpBuffer: *mut c_char, uSize: u32) -> u32;
    fn OutputDebugStringA(lpOutputString: *const c_char);
}

/// Full path of the real d3d9.dll. The folder comes from GetSystemDirectoryA so it also works when Windows
/// is not installed in C:\Windows (for a 32 bit process this is SysWOW64 on a 64 bit Windows).
fn system_d3d9_path() -> CString {
    let mut buffer = [0 as c_char; MAX_PATH];
    let len = unsafe { GetSystemDirectoryA(buffer.as_mut_ptr(), MAX_PATH as u32) } as usize;
    // raw bytes on purpose, the path is in the ANSI code page and must not go through utf8
    let mut path: Vec<u8> = if len > 0 && len < MAX_PATH {
        buffer[..len].iter().map(|&c| c as u8).collect()
    } else {
        b"C:\\Windows\\System32".to_vec()
    };
    path.extend_from_slice(b"\\d3d9.dll");
    CString::new(path).unwrap_or_else(|_| CString::new("d3d9.dll").unwrap())
}

fn debug_log(message: &str) {
    if let Ok(text) = CString::new(format!("[KaamoClubModApi proxy] {}\n", message)) {
        unsafe { OutputDebugStringA(text.as_ptr()) };
    }
}

type _D3DCreate9 = extern "stdcall" fn(UINT) -> *mut d3d9::IDirect3D9;
type _D3DPERF_SetOptions = extern "stdcall" fn(DWORD);

static mut hOriginal: HINSTANCE = ptr::null_mut();
static mut pDirect3DCreate9: Option<_D3DCreate9> = None;
static mut pD3DPERF_SetOptions: Option<_D3DPERF_SetOptions> = None;

#[no_mangle]
pub unsafe extern "system" fn D3DPERF_SetOptions(dwOptions: DWORD) {
    match pD3DPERF_SetOptions {
        Some(func) => func(dwOptions),
        // a panic here would abort the whole game, there is nothing useful to do without the real function
        None => ()
    }
}

#[no_mangle]
pub unsafe extern "stdcall" fn Direct3DCreate9(SDKVersion: UINT) -> *mut IDirect3D9 {
    match pDirect3DCreate9 {
        Some(func) => func(SDKVersion),
        // null is what the real function returns on failure, the game reports it its own way
        None => ptr::null_mut()
    }
}

unsafe extern "system" fn load_payload(_: LPVOID) -> DWORD {
    consoleapi::AllocConsole();
    println!("[*] Loading KaamoClubModAPI...");
    println!("[*] Loading core modapi dll...");

    let payload_path = match CString::new("kaamoclubmodapi.dll") {
        Ok(s) => s,
        Err(_) => {
            println!("[-] CString creation failed");
            return 1;
        }
    };
    let coremodapi = LoadLibraryA(payload_path.as_ptr());
    if coremodapi.is_null() {
        println!("[-] Failed to load the core modapi dll");
    }

    0
}

fn initialize() {
    unsafe {
        hOriginal = LoadLibraryA(system_d3d9_path().as_ptr());
        if hOriginal.is_null() {
            // no panic: inside DllMain it kills the process without any message
            debug_log("could not load the real d3d9.dll from the system folder");
            return;
        }
        let create = GetProcAddress(hOriginal, CString::new("Direct3DCreate9").unwrap().as_ptr());
        if !create.is_null() {
            pDirect3DCreate9 = Some(mem::transmute(create));
        } else {
            debug_log("Direct3DCreate9 was not found in the real d3d9.dll");
        }
        let setoptions = GetProcAddress(hOriginal, CString::new("D3DPERF_SetOptions").unwrap().as_ptr());
        if !setoptions.is_null() {
            pD3DPERF_SetOptions = Some(mem::transmute(setoptions));
        }
        let mut buffer = [0i8; MAX_PATH];
        let len = GetModuleFileNameA(ptr::null_mut(), buffer.as_mut_ptr(), MAX_PATH as u32);
        if len > 0 {
            let slice = std::slice::from_raw_parts(buffer.as_ptr() as *const u8, len as usize);
            let path_str = String::from_utf8_lossy(slice).to_lowercase();
            if path_str.contains("gof2launcher.exe") {
                return;
            }
        }
        let thread = CreateThread(ptr::null_mut(), 0, Some(load_payload), ptr::null_mut(), 0, ptr::null_mut(),);
        if thread.is_null() {
            debug_log("failed to create the loader thread, the mod api will not start");
            return;
        }
        winapi::um::handleapi::CloseHandle(thread);
    }
}

#[no_mangle]
#[allow(non_snake_case, unused_variables)]
extern "system" fn DllMain(dll_module: HINSTANCE, call_reason: DWORD, reserved: LPVOID) -> BOOL {
    const DLL_PROCESS_ATTACH: DWORD = 1;
    const DLL_PROCESS_DETACH: DWORD = 0;

    match call_reason {
        DLL_PROCESS_ATTACH => initialize(),
        DLL_PROCESS_DETACH => (),
        _ => ()
    }

    minwindef::TRUE
}