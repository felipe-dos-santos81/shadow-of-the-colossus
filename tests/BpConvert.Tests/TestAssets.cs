public static class TestAssets
{
    public static string Root =>
        Environment.GetEnvironmentVariable("SOTC_GAME_ROOT")
        ?? throw new InvalidOperationException("Set SOTC_GAME_ROOT to the extracted game tree.");
    public static string Sample(string rel) => Path.Combine(Root, rel);
}
