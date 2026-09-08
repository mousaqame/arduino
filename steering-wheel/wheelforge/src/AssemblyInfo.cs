using System.Reflection;
using System.Runtime.InteropServices;

// Fills in the Details tab of the executable's properties. Without these an
// installed app shows a blank publisher and no version, which is exactly what
// makes something look untrustworthy when a user right-clicks it.
[assembly: AssemblyTitle("WheelForge")]
[assembly: AssemblyDescription("Configuration and calibration for DIY force feedback wheels")]
[assembly: AssemblyProduct("WheelForge")]
[assembly: AssemblyCompany("Gravixar Workshop")]
[assembly: AssemblyCopyright("Open source. Free to use and modify.")]

[assembly: AssemblyVersion("0.3.0.0")]
[assembly: AssemblyFileVersion("0.3.0.0")]

[assembly: ComVisible(false)]

