param(
  [ValidateSet('apply','selftest')][string]$Action = 'apply',
  [string]$ExePath = '',
  [ValidateSet('default','K','L','M')][string]$Preset = 'default',
  [string]$FriendlyName = ''
)

$ErrorActionPreference = 'Stop'

$source = @'
using System;
using System.IO;
using System.Runtime.InteropServices;

namespace XiaofengNvapi {
  public sealed class ApplyResult {
    public bool ok { get; set; }
    public string profile { get; set; }
    public bool created { get; set; }
    public string preset { get; set; }
    public uint rawPreset { get; set; }
    public bool cleared { get; set; }
    public string error { get; set; }
  }

  [StructLayout(LayoutKind.Explicit, Size = 4100)]
  public struct NVDRS_VALUE {
    [FieldOffset(0)] public uint u32Value;
  }

  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct NVDRS_SETTING {
    public uint version;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 2048)] public string settingName;
    public uint settingId;
    public int settingType;
    public int settingLocation;
    public uint isCurrentPredefined;
    public uint isPredefinedValid;
    public NVDRS_VALUE predefinedValue;
    public NVDRS_VALUE currentValue;
  }

  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct NVDRS_PROFILE {
    public uint version;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 2048)] public string profileName;
    public uint gpuSupport;
    public uint isPredefined;
    public uint numOfApps;
    public uint numOfSettings;
  }

  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct NVDRS_APPLICATION {
    public uint version;
    public uint isPredefined;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 2048)] public string appName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 2048)] public string userFriendlyName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 2048)] public string launcher;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 2048)] public string fileInFolder;
    public uint flags;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 2048)] public string commandLine;
  }

  public static class Drs {
    const int OK = 0;
    const int PROFILE_NOT_FOUND = -163;
    const int SETTING_ALREADY_DEFAULT = -160;
    const int SETTING_NOT_FOUND = -165;
    const int EXECUTABLE_NOT_FOUND = -166;

    const uint SR_OVERRIDE_ID = 0x10E41E01;
    const uint SR_PRESET_ID = 0x10E41DF3;

    const uint ID_INITIALIZE = 0x0150E828;
    const uint ID_UNLOAD = 0xD22BDD7E;
    const uint ID_DRS_CREATE_SESSION = 0x0694D52E;
    const uint ID_DRS_DESTROY_SESSION = 0xDAD9CFF8;
    const uint ID_DRS_LOAD_SETTINGS = 0x375DBD6B;
    const uint ID_DRS_SAVE_SETTINGS = 0xFCBC7E14;
    const uint ID_DRS_SET_SETTING = 0x577DD202;
    const uint ID_DRS_GET_SETTING = 0x73BF8338;
    const uint ID_DRS_DELETE_PROFILE_SETTING = 0xE4A26362;
    const uint ID_DRS_CREATE_PROFILE = 0xCC176068;
    const uint ID_DRS_FIND_PROFILE_BY_NAME = 0x7E4A9A0B;
    const uint ID_DRS_GET_PROFILE_INFO = 0x61CD6FD6;
    const uint ID_DRS_CREATE_APPLICATION = 0x4347A9DE;
    const uint ID_DRS_FIND_APPLICATION_BY_NAME = 0xEEE566B2;

    [DllImport("nvapi64.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern IntPtr nvapi_QueryInterface(uint id);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int SimpleFn();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int SessionCreateFn(out IntPtr session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int SessionFn(IntPtr session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int SetSettingFn(IntPtr session, IntPtr profile, ref NVDRS_SETTING setting);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int GetSettingFn(IntPtr session, IntPtr profile, uint settingId, ref NVDRS_SETTING setting);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int DeleteSettingFn(IntPtr session, IntPtr profile, uint settingId);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int CreateProfileFn(IntPtr session, ref NVDRS_PROFILE profile, out IntPtr handle);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int FindProfileFn(IntPtr session, [MarshalAs(UnmanagedType.LPWStr)] string profileName, out IntPtr handle);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int GetProfileInfoFn(IntPtr session, IntPtr profile, ref NVDRS_PROFILE info);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int CreateApplicationFn(IntPtr session, IntPtr profile, ref NVDRS_APPLICATION app);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int FindApplicationFn(IntPtr session, [MarshalAs(UnmanagedType.LPWStr)] string appName, out IntPtr profile, ref NVDRS_APPLICATION app);

    static T Resolve<T>(uint id) where T : class {
      IntPtr address = nvapi_QueryInterface(id);
      if (address == IntPtr.Zero) throw new InvalidOperationException(String.Format("NvAPI interface 0x{0:X8} unavailable", id));
      return Marshal.GetDelegateForFunctionPointer(address, typeof(T)) as T;
    }

    static uint Ver(Type type, int version) {
      return ((uint)Marshal.SizeOf(type)) | ((uint)version << 16);
    }

    static NVDRS_SETTING NewSetting() {
      NVDRS_SETTING value = new NVDRS_SETTING();
      value.version = Ver(typeof(NVDRS_SETTING), 1);
      value.settingName = String.Empty;
      return value;
    }

    static NVDRS_PROFILE NewProfile() {
      NVDRS_PROFILE value = new NVDRS_PROFILE();
      value.version = Ver(typeof(NVDRS_PROFILE), 1);
      value.profileName = String.Empty;
      return value;
    }

    static NVDRS_APPLICATION NewApplication() {
      NVDRS_APPLICATION value = new NVDRS_APPLICATION();
      value.version = Ver(typeof(NVDRS_APPLICATION), 4);
      value.appName = String.Empty;
      value.userFriendlyName = String.Empty;
      value.launcher = String.Empty;
      value.fileInFolder = String.Empty;
      value.commandLine = String.Empty;
      return value;
    }

    static void Check(int status, string operation) {
      if (status != OK) throw new InvalidOperationException(String.Format("{0} failed ({1})", operation, status));
    }

    static void SetDword(SetSettingFn fn, IntPtr session, IntPtr profile, uint id, uint value) {
      NVDRS_SETTING setting = NewSetting();
      setting.settingId = id;
      setting.settingType = 0;
      setting.currentValue.u32Value = value;
      Check(fn(session, profile, ref setting), String.Format("NvAPI_DRS_SetSetting 0x{0:X8}", id));
    }

    static void DeleteSetting(DeleteSettingFn fn, IntPtr session, IntPtr profile, uint id) {
      int status = fn(session, profile, id);
      if (status != OK && status != SETTING_NOT_FOUND && status != SETTING_ALREADY_DEFAULT)
        throw new InvalidOperationException(String.Format("NvAPI_DRS_DeleteProfileSetting 0x{0:X8} failed ({1})", id, status));
    }

    static uint ReadDword(GetSettingFn fn, IntPtr session, IntPtr profile, uint id) {
      NVDRS_SETTING setting = NewSetting();
      int status = fn(session, profile, id, ref setting);
      if (status != OK) throw new InvalidOperationException(String.Format("NvAPI_DRS_GetSetting 0x{0:X8} failed ({1})", id, status));
      return setting.currentValue.u32Value;
    }

    static IntPtr ResolveProfile(
      IntPtr session, string exePath, bool create, string friendlyName,
      FindApplicationFn findApplication, FindProfileFn findProfile,
      GetProfileInfoFn getProfileInfo, CreateProfileFn createProfile,
      CreateApplicationFn createApplication, out string profileName, out bool created) {
      profileName = String.Empty;
      created = false;
      string full = Path.GetFullPath(exePath);
      NVDRS_APPLICATION foundApp = NewApplication();
      IntPtr profile;
      int status = findApplication(session, full, out profile, ref foundApp);
      if (status == OK) {
        NVDRS_PROFILE info = NewProfile();
        if (getProfileInfo(session, profile, ref info) == OK) profileName = info.profileName ?? String.Empty;
        return profile;
      }
      if (status != EXECUTABLE_NOT_FOUND && status != PROFILE_NOT_FOUND)
        throw new InvalidOperationException(String.Format("NvAPI_DRS_FindApplicationByName failed ({0})", status));
      if (!create) return IntPtr.Zero;

      string exeName = Path.GetFileName(full);
      string requestedName = String.IsNullOrWhiteSpace(friendlyName) ? ("Xiaofeng DLSS5 - " + exeName) : friendlyName.Trim();
      if (requestedName.Length > 160) requestedName = requestedName.Substring(0, 160);
      status = findProfile(session, requestedName, out profile);
      if (status == PROFILE_NOT_FOUND) {
        NVDRS_PROFILE newProfile = NewProfile();
        newProfile.profileName = requestedName;
        newProfile.gpuSupport = 1;
        Check(createProfile(session, ref newProfile, out profile), "NvAPI_DRS_CreateProfile");
        created = true;
      } else if (status != OK) {
        throw new InvalidOperationException(String.Format("NvAPI_DRS_FindProfileByName failed ({0})", status));
      }

      NVDRS_APPLICATION app = NewApplication();
      app.appName = exeName;
      app.userFriendlyName = requestedName;
      Check(createApplication(session, profile, ref app), "NvAPI_DRS_CreateApplication");
      profileName = requestedName;
      return profile;
    }

    static uint PresetValue(string preset) {
      string value = (preset ?? String.Empty).Trim().ToUpperInvariant();
      if (value == "K") return 11;
      if (value == "L") return 12;
      if (value == "M") return 13;
      if (value == "DEFAULT") return 0;
      throw new ArgumentException("Unsupported SR preset: " + preset);
    }

    public static string LayoutSummary() {
      int setting = Marshal.SizeOf(typeof(NVDRS_SETTING));
      int profile = Marshal.SizeOf(typeof(NVDRS_PROFILE));
      int application = Marshal.SizeOf(typeof(NVDRS_APPLICATION));
      if (setting != 12320 || profile != 4116 || application != 20492)
        throw new InvalidOperationException(String.Format("Unexpected NvAPI struct layout {0}/{1}/{2}", setting, profile, application));
      return String.Format("NVDRS_SETTING={0};NVDRS_PROFILE={1};NVDRS_APPLICATION_V4={2}", setting, profile, application);
    }

    public static ApplyResult Apply(string exePath, string preset, string friendlyName) {
      ApplyResult result = new ApplyResult();
      result.preset = preset ?? String.Empty;
      result.error = String.Empty;
      if (String.IsNullOrWhiteSpace(exePath) || !Path.IsPathRooted(exePath) || !String.Equals(Path.GetExtension(exePath), ".exe", StringComparison.OrdinalIgnoreCase)) {
        result.error = "Invalid game executable path";
        return result;
      }

      IntPtr session = IntPtr.Zero;
      bool initialized = false;
      try {
        SimpleFn initialize = Resolve<SimpleFn>(ID_INITIALIZE);
        SimpleFn unload = Resolve<SimpleFn>(ID_UNLOAD);
        SessionCreateFn createSession = Resolve<SessionCreateFn>(ID_DRS_CREATE_SESSION);
        SessionFn destroySession = Resolve<SessionFn>(ID_DRS_DESTROY_SESSION);
        SessionFn loadSettings = Resolve<SessionFn>(ID_DRS_LOAD_SETTINGS);
        SessionFn saveSettings = Resolve<SessionFn>(ID_DRS_SAVE_SETTINGS);
        SetSettingFn setSetting = Resolve<SetSettingFn>(ID_DRS_SET_SETTING);
        GetSettingFn getSetting = Resolve<GetSettingFn>(ID_DRS_GET_SETTING);
        DeleteSettingFn deleteSetting = Resolve<DeleteSettingFn>(ID_DRS_DELETE_PROFILE_SETTING);
        CreateProfileFn createProfile = Resolve<CreateProfileFn>(ID_DRS_CREATE_PROFILE);
        FindProfileFn findProfile = Resolve<FindProfileFn>(ID_DRS_FIND_PROFILE_BY_NAME);
        GetProfileInfoFn getProfileInfo = Resolve<GetProfileInfoFn>(ID_DRS_GET_PROFILE_INFO);
        CreateApplicationFn createApplication = Resolve<CreateApplicationFn>(ID_DRS_CREATE_APPLICATION);
        FindApplicationFn findApplication = Resolve<FindApplicationFn>(ID_DRS_FIND_APPLICATION_BY_NAME);

        Check(initialize(), "NvAPI_Initialize");
        initialized = true;
        Check(createSession(out session), "NvAPI_DRS_CreateSession");
        Check(loadSettings(session), "NvAPI_DRS_LoadSettings");

        bool clear = String.Equals(preset, "default", StringComparison.OrdinalIgnoreCase);
        string profileName;
        bool created;
        IntPtr profile = ResolveProfile(session, exePath, !clear, friendlyName,
          findApplication, findProfile, getProfileInfo, createProfile, createApplication,
          out profileName, out created);
        result.profile = profileName;
        result.created = created;

        if (clear) {
          if (profile != IntPtr.Zero) {
            DeleteSetting(deleteSetting, session, profile, SR_PRESET_ID);
            DeleteSetting(deleteSetting, session, profile, SR_OVERRIDE_ID);
            Check(saveSettings(session), "NvAPI_DRS_SaveSettings");
          }
          result.ok = true;
          result.cleared = true;
          result.rawPreset = 0;
        } else {
          uint raw = PresetValue(preset);
          SetDword(setSetting, session, profile, SR_OVERRIDE_ID, 1);
          SetDword(setSetting, session, profile, SR_PRESET_ID, raw);
          Check(saveSettings(session), "NvAPI_DRS_SaveSettings");
          uint verifyEnable = ReadDword(getSetting, session, profile, SR_OVERRIDE_ID);
          uint verifyPreset = ReadDword(getSetting, session, profile, SR_PRESET_ID);
          if (verifyEnable != 1 || verifyPreset != raw)
            throw new InvalidOperationException(String.Format("NvAPI read-back mismatch enable={0} preset={1}", verifyEnable, verifyPreset));
          result.ok = true;
          result.rawPreset = raw;
        }

        destroySession(session);
        session = IntPtr.Zero;
        unload();
        initialized = false;
        return result;
      } catch (Exception ex) {
        result.ok = false;
        result.error = ex.Message;
        return result;
      } finally {
        if (session != IntPtr.Zero) {
          try { Resolve<SessionFn>(ID_DRS_DESTROY_SESSION)(session); } catch { }
        }
        if (initialized) {
          try { Resolve<SimpleFn>(ID_UNLOAD)(); } catch { }
        }
      }
    }
  }
}
'@

try {
  Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
  if ($Action -eq 'selftest') {
    $layout = [XiaofengNvapi.Drs]::LayoutSummary()
    [pscustomobject]@{ ok = $true; layout = $layout } | ConvertTo-Json -Compress
    exit 0
  }

  $result = [XiaofengNvapi.Drs]::Apply($ExePath, $Preset, $FriendlyName)
  $result | ConvertTo-Json -Compress
  if ($result.ok) { exit 0 }
  exit 2
} catch {
  [pscustomobject]@{ ok = $false; error = $_.Exception.Message; code = 'NVAPI_HELPER_EXCEPTION' } | ConvertTo-Json -Compress
  exit 1
}
