namespace ZoltDumpUI.Services;

/// <summary>Minimal Valve VDF (KeyValues) format parser for Steam library detection.</summary>
public static class VdfParser
{
    public static List<string> ExtractLibraryPaths(string vdfContent)
    {
        var paths = new List<string>();
        var tokens = Tokenize(vdfContent);
        // Extract "path" values at depth 2 (inside "libraryfolders" -> numbered keys)
        var depth = 0;
        for (var i = 0; i < tokens.Count; i++)
        {
            var t = tokens[i];
            if (t == "{") depth++;
            else if (t == "}") depth--;
            else if (depth == 2 && t.Equals("path", StringComparison.OrdinalIgnoreCase) && i + 1 < tokens.Count)
            {
                paths.Add(tokens[++i].Replace("\\\\", "\\"));
            }
        }
        return paths;
    }

    private static List<string> Tokenize(string content)
    {
        var tokens = new List<string>();
        var i = 0;
        while (i < content.Length)
        {
            var c = content[i];
            if (char.IsWhiteSpace(c)) { i++; continue; }
            if (c == '/' && i + 1 < content.Length && content[i + 1] == '/')
            {
                while (i < content.Length && content[i] != '\n') i++;
                continue;
            }
            if (c is '{' or '}') { tokens.Add(c.ToString()); i++; continue; }
            if (c == '"')
            {
                i++;
                var start = i;
                var sb = new System.Text.StringBuilder();
                while (i < content.Length && content[i] != '"')
                {
                    if (content[i] == '\\' && i + 1 < content.Length) { sb.Append(content[++i]); }
                    else { sb.Append(content[i]); }
                    i++;
                }
                if (i < content.Length) i++; // skip closing quote
                tokens.Add(sb.ToString());
            }
            else
            {
                var start = i;
                while (i < content.Length && !char.IsWhiteSpace(content[i]) && content[i] is not ('{' or '}' or '"'))
                    i++;
                tokens.Add(content[start..i]);
            }
        }
        return tokens;
    }
}
