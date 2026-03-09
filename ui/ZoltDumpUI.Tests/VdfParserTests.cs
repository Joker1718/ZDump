using Xunit;
using ZoltDumpUI.Services;

namespace ZoltDumpUI.Tests;

public class VdfParserTests
{
    [Fact]
    public void ExtractLibraryPaths_SingleLibrary()
    {
        const string vdf = """
            "libraryfolders"
            {
                "0"
                {
                    "path"		"C:\\Program Files (x86)\\Steam"
                    "label"		""
                    "apps"
                    {
                        "228980"		"0"
                    }
                }
            }
            """;

        var paths = VdfParser.ExtractLibraryPaths(vdf);
        Assert.Single(paths);
        Assert.Equal(@"C:\Program Files (x86)\Steam", paths[0]);
    }

    [Fact]
    public void ExtractLibraryPaths_MultipleLibraries()
    {
        const string vdf = """
            "libraryfolders"
            {
                "0"
                {
                    "path"		"C:\\Program Files (x86)\\Steam"
                }
                "1"
                {
                    "path"		"D:\\SteamLibrary"
                }
                "2"
                {
                    "path"		"E:\\Games\\Steam"
                }
            }
            """;

        var paths = VdfParser.ExtractLibraryPaths(vdf);
        Assert.Equal(3, paths.Count);
        Assert.Equal(@"C:\Program Files (x86)\Steam", paths[0]);
        Assert.Equal(@"D:\SteamLibrary", paths[1]);
        Assert.Equal(@"E:\Games\Steam", paths[2]);
    }

    [Fact]
    public void ExtractLibraryPaths_EmptyContent_ReturnsEmpty()
    {
        Assert.Empty(VdfParser.ExtractLibraryPaths(""));
    }

    [Fact]
    public void ExtractLibraryPaths_NoPathKeys_ReturnsEmpty()
    {
        const string vdf = """
            "libraryfolders"
            {
                "0"
                {
                    "label"		"main"
                }
            }
            """;

        Assert.Empty(VdfParser.ExtractLibraryPaths(vdf));
    }

    [Fact]
    public void ExtractLibraryPaths_IgnoresComments()
    {
        const string vdf = """
            // Steam library config
            "libraryfolders"
            {
                "0"
                {
                    // Main library
                    "path"		"C:\\Steam"
                }
            }
            """;

        var paths = VdfParser.ExtractLibraryPaths(vdf);
        Assert.Single(paths);
        Assert.Equal(@"C:\Steam", paths[0]);
    }

    [Fact]
    public void ExtractLibraryPaths_IgnoresPathAtWrongDepth()
    {
        // "path" at depth 1 (directly under libraryfolders) should be ignored
        const string vdf = """
            "libraryfolders"
            {
                "path"		"SHOULD_BE_IGNORED"
                "0"
                {
                    "path"		"C:\\Valid"
                }
            }
            """;

        var paths = VdfParser.ExtractLibraryPaths(vdf);
        Assert.Single(paths);
        Assert.Equal(@"C:\Valid", paths[0]);
    }

    [Fact]
    public void ExtractLibraryPaths_HandlesEscapedBackslashes()
    {
        const string vdf = """
            "libraryfolders"
            {
                "0"
                {
                    "path"		"D:\\\\Games\\\\Steam"
                }
            }
            """;

        var paths = VdfParser.ExtractLibraryPaths(vdf);
        Assert.Single(paths);
        Assert.Equal(@"D:\Games\Steam", paths[0]);
    }
}
