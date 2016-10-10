/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CONFIG_PARSER_HXX
#define CONFIG_PARSER_HXX

#include <boost/filesystem.hpp>

#include <memory>
#include <map>

class LineParser;

class ConfigParser {
public:
    virtual ~ConfigParser() {}

    virtual bool PreParseLine(LineParser &line);
    virtual void ParseLine(LineParser &line) = 0;
    virtual void Finish() {}
};

/**
 * A #ConfigParser which can dynamically forward method calls to a
 * nested #ConfigParser instance.
 */
class NestedConfigParser : public ConfigParser {
    std::unique_ptr<ConfigParser> child;

public:
    /* virtual methods from class ConfigParser */
    bool PreParseLine(LineParser &line) override;
    void ParseLine(LineParser &line) final;
    void Finish() override;

protected:
    void SetChild(std::unique_ptr<ConfigParser> &&_child);
    virtual void ParseLine2(LineParser &line) = 0;
};

/**
 * A #ConfigParser which ignores lines starting with '#'.
 */
class CommentConfigParser final : public ConfigParser {
    ConfigParser &child;

public:
    explicit CommentConfigParser(ConfigParser &_child)
        :child(_child) {}

    /* virtual methods from class ConfigParser */
    bool PreParseLine(LineParser &line) override;
    void ParseLine(LineParser &line) final;
    void Finish() override;
};

/**
 * A #ConfigParser which can define and use variables.
 */
class VariableConfigParser final : public ConfigParser {
    ConfigParser &child;

    std::map<std::string, std::string> variables;

    mutable std::string buffer;

public:
    explicit VariableConfigParser(ConfigParser &_child)
        :child(_child) {}

    /* virtual methods from class ConfigParser */
    bool PreParseLine(LineParser &line) override;
    void ParseLine(LineParser &line) final;
    void Finish() override;

private:
    void ExpandOne(std::string &dest,
                   const char *&src, const char *end) const;
    void ExpandQuoted(std::string &dest,
                      const char *src, const char *end) const;
    void Expand(std::string &dest, const char *src) const;
    char *Expand(const char *src) const;
    void Expand(LineParser &line) const;
};

/**
 * A #ConfigParser which can "include" other files.
 */
class IncludeConfigParser final : public ConfigParser {
    const boost::filesystem::path path;

    ConfigParser &child;

public:
    IncludeConfigParser(boost::filesystem::path &&_path, ConfigParser &_child)
        :path(std::move(_path)), child(_child) {}

    /* virtual methods from class ConfigParser */
    bool PreParseLine(LineParser &line) override;
    void ParseLine(LineParser &line) override;
    void Finish() override;

private:
    void IncludePath(boost::filesystem::path &&p);
    void IncludeOptionalPath(boost::filesystem::path &&p);
};

void
ParseConfigFile(const boost::filesystem::path &path, ConfigParser &parser);

#endif
