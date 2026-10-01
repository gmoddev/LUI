using Lui.Packaging;

if (args.Length is < 3 or > 4)
    throw new ArgumentException("Usage: <manifest> <published-host-dir> <output-dir> [extension-dir]");
PackageBuilder.Stage(args[0], args[1], args[2], args.Length == 4 ? args[3] : null);
