#include "AiMelody.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <tuple>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace sonora::ai
{
namespace
{
bool isExecutableFile(const juce::File& file)
{
    return file.existsAsFile() && ::access(file.getFullPathName().toRawUTF8(), X_OK) == 0;
}

// Track names and prompts are user text embedded in the model message:
// strip control characters (keeps newlines/tabs) and bound the length.
juce::String cleanText(const juce::String& text, int maxChars)
{
    juce::String out;
    for (auto c : text)
        if (c == '\n' || c == '\t' || c >= 0x20)
            out += juce::String::charToString(c);
    return out.substring(0, maxChars).trim();
}

juce::String noteName(int pitch) { return juce::MidiMessage::getMidiNoteName(pitch, true, true, 4); }

juce::String instrumentDescription(const Track& track)
{
    if (track.kind == TrackKind::Drums)
        return "drum kit (8 pads)";
    if (!validInstrument(track.instrumentPreset))
        return "synth";
    if (track.instrumentPreset == 0)
    {
        for (const auto& patch : synthPatches())
            if (patch.params == track.synth)
                return juce::String("Sonora Synth, patch \"") + patch.name + "\"";
        return juce::String("Sonora Synth (custom patch, ") + synthWaveName(track.synth.wave) + " oscillator)";
    }
    return instruments[static_cast<std::size_t>(track.instrumentPreset)].name;
}

juce::String describeNotes(const Pattern& pattern)
{
    if (pattern.count == 0)
        return "    (empty)\n";
    std::vector<Note> notes(pattern.notes.begin(), pattern.notes.begin() + pattern.count);
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch);
    });
    juce::String out;
    for (const auto& note : notes)
        out << "    start=" << note.start << " dur=" << note.duration << " pitch=" << note.pitch
            << " (" << noteName(note.pitch) << ") vel=" << note.velocity << "\n";
    return out;
}

juce::String describeDrums(const DrumPattern& drums)
{
    juce::String out;
    for (int pad = 0; pad < drumPads; ++pad)
    {
        juce::StringArray steps;
        for (int step = 0; step < gridSteps; ++step)
            if (drums.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(step)] > 0)
                steps.add(juce::String(step));
        if (!steps.isEmpty())
            out << "    " << drumNames[static_cast<std::size_t>(pad)] << " on sixteenth steps: "
                << steps.joinIntoString(",") << "\n";
    }
    return out.isEmpty() ? juce::String("    (empty)\n") : out;
}

// Clamps a pitch into the full MIDI range (the piano roll covers 0-127).
int foldPitch(int pitch)
{
    return std::clamp(pitch, lowestPitch, highestPitch);
}
}

juce::File findClaudeExecutable()
{
    const auto override = juce::SystemStats::getEnvironmentVariable("SONORA_CLAUDE_PATH", {});
    if (override.isNotEmpty() && juce::File::isAbsolutePath(override))
    {
        const juce::File file(override);
        if (isExecutableFile(file))
            return file;
    }
    // Only absolute PATH entries: a relative entry such as "." would let a
    // file in the current directory impersonate the CLI.
    for (const auto& dir : juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("PATH", {}), ":", {}))
        if (juce::File::isAbsolutePath(dir))
        {
            const auto candidate = juce::File(dir).getChildFile("claude");
            if (isExecutableFile(candidate))
                return candidate;
        }
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    for (const auto& relative : { ".local/bin/claude", ".claude/local/claude",
                                  ".local/share/mise/installs/claude/latest/claude",
                                  ".npm-global/bin/claude", ".bun/bin/claude" })
    {
        const auto candidate = home.getChildFile(relative);
        if (isExecutableFile(candidate))
            return candidate;
    }
    for (const auto* absolute : { "/usr/local/bin/claude", "/usr/bin/claude" })
        if (isExecutableFile(juce::File(absolute)))
            return juce::File(absolute);
    return {};
}

juce::StringArray sanitizedEnvironment(const juce::StringArray& parentEnvironment)
{
    juce::StringArray result;
    for (const auto& entry : parentEnvironment)
        if (!entry.startsWith("ANTHROPIC_API_KEY=") && !entry.startsWith("ANTHROPIC_AUTH_TOKEN="))
            result.add(entry);
    return result;
}

ProcessResult runProcess(const juce::File& executable, const juce::StringArray& arguments,
                         const juce::String& input, const juce::File& workDir,
                         const juce::StringArray& environment, int timeoutMs,
                         const std::atomic<bool>* cancel)
{
    ProcessResult result;
    if (!juce::File::isAbsolutePath(executable.getFullPathName()) || !isExecutableFile(executable))
    {
        result.failure = "Executable not found.";
        return result;
    }
    int inPipe[2] = { -1, -1 }, outPipe[2] = { -1, -1 }, errPipe[2] = { -1, -1 };
    if (::pipe2(inPipe, O_CLOEXEC) != 0 || ::pipe2(outPipe, O_CLOEXEC) != 0 || ::pipe2(errPipe, O_CLOEXEC) != 0)
    {
        for (int fd : { inPipe[0], inPipe[1], outPipe[0], outPipe[1], errPipe[0], errPipe[1] })
            if (fd >= 0)
                ::close(fd);
        result.failure = "Could not create pipes.";
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, inPipe[0], 0);
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], 1);
    posix_spawn_file_actions_adddup2(&actions, errPipe[1], 2);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    posix_spawn_file_actions_addclosefrom_np(&actions, 3); // no inherited app descriptors
#endif
    posix_spawn_file_actions_addchdir_np(&actions, workDir.getFullPathName().toRawUTF8());

    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t emptyMask, defaults;
    sigemptyset(&emptyMask);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    posix_spawnattr_setsigmask(&attributes, &emptyMask);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setpgroup(&attributes, 0); // own group: timeouts kill helpers too
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP);

    std::vector<std::string> argStorage { executable.getFullPathName().toStdString() };
    for (const auto& argument : arguments)
        argStorage.push_back(argument.toStdString());
    std::vector<char*> argv;
    for (auto& argument : argStorage)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::vector<std::string> envStorage;
    for (const auto& entry : environment)
        envStorage.push_back(entry.toStdString());
    std::vector<char*> envp;
    for (auto& entry : envStorage)
        envp.push_back(entry.data());
    envp.push_back(nullptr);

    // Writing to a child that exited early raises SIGPIPE: block it on this
    // thread and consume any pending one before restoring the mask.
    sigset_t pipeSet, previousMask;
    sigemptyset(&pipeSet);
    sigaddset(&pipeSet, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipeSet, &previousMask);

    pid_t pid = -1;
    const int spawned = ::posix_spawn(&pid, argStorage[0].c_str(), &actions, &attributes, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    ::close(inPipe[0]);
    ::close(outPipe[1]);
    ::close(errPipe[1]);
    if (spawned != 0)
    {
        ::close(inPipe[1]);
        ::close(outPipe[0]);
        ::close(errPipe[0]);
        pthread_sigmask(SIG_SETMASK, &previousMask, nullptr);
        result.failure = "Could not start process (" + juce::String(std::strerror(spawned)) + ").";
        return result;
    }

    for (int fd : { inPipe[1], outPipe[0], errPipe[0] })
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    const auto payload = input.toStdString();
    std::size_t written = 0;
    int inFd = inPipe[1], outFd = outPipe[0], errFd = errPipe[0];
    if (payload.empty())
    {
        ::close(inFd);
        inFd = -1;
    }
    std::string out, err;
    constexpr std::size_t outputCap = 8u << 20;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    bool killed = false;
    while (outFd >= 0 || errFd >= 0 || inFd >= 0)
    {
        if (cancel != nullptr && cancel->load())
        {
            result.cancelled = killed = true;
            break;
        }
        if (juce::Time::getMillisecondCounterHiRes() > deadline)
        {
            result.timedOut = killed = true;
            break;
        }
        pollfd fds[3];
        int count = 0;
        if (inFd >= 0) fds[count++] = { inFd, POLLOUT, 0 };
        if (outFd >= 0) fds[count++] = { outFd, POLLIN, 0 };
        if (errFd >= 0) fds[count++] = { errFd, POLLIN, 0 };
        if (::poll(fds, static_cast<nfds_t>(count), 100) < 0 && errno != EINTR)
            break;
        for (int i = 0; i < count; ++i)
        {
            if (fds[i].revents == 0)
                continue;
            if (fds[i].fd == inFd)
            {
                const auto n = ::write(inFd, payload.data() + written, payload.size() - written);
                if (n > 0)
                    written += static_cast<std::size_t>(n);
                if ((n < 0 && errno != EAGAIN) || written >= payload.size())
                {
                    ::close(inFd);
                    inFd = -1;
                }
                continue;
            }
            char buffer[65536];
            const auto n = ::read(fds[i].fd, buffer, sizeof(buffer));
            auto& target = fds[i].fd == outFd ? out : err;
            if (n > 0)
            {
                target.append(buffer, static_cast<std::size_t>(n));
                if (target.size() > outputCap)
                {
                    result.failure = "Output too large.";
                    killed = true;
                }
            }
            else if (n == 0 || (errno != EAGAIN && errno != EINTR))
            {
                ::close(fds[i].fd);
                (fds[i].fd == outFd ? outFd : errFd) = -1;
            }
        }
        if (killed)
            break;
    }
    for (int fd : { inFd, outFd, errFd })
        if (fd >= 0)
            ::close(fd);

    int status = 0;
    if (killed)
    {
        ::kill(-pid, SIGTERM);
        for (int i = 0; i < 20 && ::waitpid(pid, &status, WNOHANG) == 0; ++i)
            juce::Thread::sleep(50);
        if (::waitpid(pid, &status, WNOHANG) == 0)
        {
            ::kill(-pid, SIGKILL);
            ::waitpid(pid, &status, 0);
        }
    }
    else
        ::waitpid(pid, &status, 0);
    const timespec zero { 0, 0 };
    while (sigtimedwait(&pipeSet, nullptr, &zero) > 0) {}
    pthread_sigmask(SIG_SETMASK, &previousMask, nullptr);

    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.output = juce::String::fromUTF8(out.data(), static_cast<int>(out.size()));
    result.errorOutput = juce::String::fromUTF8(err.data(), static_cast<int>(err.size()));
    return result;
}

juce::String systemPrompt()
{
    return "You are a skilled composer and arranger working inside Sonora, a music production app. "
           "You write one MIDI part for a 4-bar loop that complements the other tracks: same key and "
           "harmony, a rhythm that locks with the drums, and a register and density that leave room "
           "for the existing parts instead of doubling them. Follow the user's description of the part.\n\n"
           "Timing: 960 ticks per quarter note, 4/4, one sixteenth = 240 ticks, one bar = 3840 ticks, "
           "loop length = 15360 ticks. Every note needs start >= 0, duration >= 60, and "
           "start + duration <= 15360. Prefer starts and durations on a 120-tick grid.\n"
           "Pitch: full MIDI range 0 to 127; 60 is middle C (C4). Prefer the C2-C6 area unless asked otherwise.\n"
           "Notes on the same pitch must not overlap in time. Velocity 1-127; use dynamics musically.\n"
           "Use between 4 and 128 notes. Chords are allowed (several pitches at once).\n"
           "Reply only with the structured result: a short title, one sentence explaining how the part "
           "fits the song, and the notes.";
}

juce::String responseSchema()
{
    return R"({"type":"object","additionalProperties":false,"required":["title","explanation","notes"],)"
           R"("properties":{"title":{"type":"string","maxLength":60},"explanation":{"type":"string","maxLength":400},)"
           R"("notes":{"type":"array","minItems":1,"maxItems":256,"items":{"type":"object","additionalProperties":false,)"
           R"("required":["start","duration","pitch","velocity"],"properties":{)"
           R"("start":{"type":"integer","minimum":0,"maximum":15359},)"
           R"("duration":{"type":"integer","minimum":1,"maximum":15360},)"
           R"("pitch":{"type":"integer","minimum":0,"maximum":127},)"
           R"("velocity":{"type":"integer","minimum":1,"maximum":127}}}}}})";
}

juce::String buildUserMessage(const MelodyRequest& request)
{
    const auto& project = request.project;
    const auto target = static_cast<std::size_t>(std::clamp(request.track, 0, maxTracks - 1));
    juce::String context;
    context << "Song: " << juce::String(project.bpm, 0) << " BPM, 4/4, 4-bar loop.\n\n";
    for (int index = 0; index < maxTracks; ++index)
    {
        const auto& track = project.tracks[static_cast<std::size_t>(index)];
        if (track.kind == TrackKind::None || static_cast<std::size_t>(index) == target)
            continue;
        context << "Track \"" << cleanText(track.trackName(), 60) << "\" (" << instrumentDescription(track) << ")"
                << (track.mix.mute ? " [muted]" : "") << ":\n";
        if (track.kind == TrackKind::Drums)
            context << describeDrums(track.drumPatterns[static_cast<std::size_t>(
                std::clamp(request.drumSlots[static_cast<std::size_t>(index)], 0, numPatterns - 1))]);
        else
            context << describeNotes(track.melodies[static_cast<std::size_t>(
                std::clamp(request.melodySlots[static_cast<std::size_t>(index)], 0, numPatterns - 1))]);
    }
    if (context.length() > maxContextChars)
        context = context.substring(0, maxContextChars) + "\n    (context truncated)\n";
    const auto& targetTrack = project.tracks[target];
    juce::String message;
    message << "Song key: " << keyName(project.musicKey) << " " << scaleName(project.musicScale)
            << ". Prefer notes in key; passing tones are fine in small doses.\n\n"
            << "Write the part for track \"" << cleanText(targetTrack.trackName(), 60) << "\" ("
            << instrumentDescription(targetTrack) << ").\n\n"
            << "Other tracks in the loop:\n" << context << "\n"
            << "The target track's current notes (you are replacing them; use them only as a hint):\n"
            << describeNotes(targetTrack.melodies[static_cast<std::size_t>(
                   std::clamp(request.melodySlots[target], 0, numPatterns - 1))])
            << "\nThe user's description of the part they want:\n\"\"\"\n"
            << cleanText(request.prompt, maxPromptChars) << "\n\"\"\"\n";
    return message;
}

namespace
{
juce::StringArray sandboxArguments(const juce::String& system, const juce::String& schema)
{
    return { "-p",
             "--output-format", "json",
             "--tools", "",                  // no built-in tools at all
             "--strict-mcp-config",          // no MCP servers
             "--setting-sources", "",        // no user/project settings or hooks
             "--no-session-persistence",
             "--system-prompt", system,
             "--json-schema", schema };
}

juce::String runSandboxedClaude(const juce::StringArray& arguments, const juce::String& input,
                                const std::atomic<bool>* cancel, int timeoutMs, juce::String& error)
{
    const auto executable = findClaudeExecutable();
    if (executable == juce::File())
    {
        error = "Claude Code was not found. Install it, run `claude` once to sign in, then try again.";
        return {};
    }
    const auto workDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile("sonora-ai", "", false);
    if (!workDir.createDirectory())
    {
        error = "Could not create a temporary folder for Claude.";
        return {};
    }
    juce::StringArray parentEnvironment;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry)
        parentEnvironment.add(juce::String::fromUTF8(*entry));
    const auto process = runProcess(executable, arguments, input, workDir,
                                    sanitizedEnvironment(parentEnvironment), timeoutMs, cancel);
    workDir.deleteRecursively();
    if (process.cancelled)
        error = "Cancelled.";
    else if (process.timedOut)
        error = "Claude took too long to answer. Try again or simplify the request.";
    else if (process.failure.isNotEmpty())
        error = process.failure;
    else if (process.exitCode != 0 && process.output.trim().isEmpty())
        error = "Claude Code exited with an error"
            + (process.errorOutput.trim().isEmpty() ? juce::String(".")
                                                    : ": " + process.errorOutput.trim().substring(0, 300));
    return process.output;
}
}

juce::StringArray claudeArguments()
{
    return sandboxArguments(systemPrompt(), responseSchema());
}

namespace
{
// The structured answer object from `claude -p --output-format json`, or a
// void var with `error` set.
juce::var extractAnswer(const juce::String& cliStdout, juce::String& error)
{
    const auto envelope = juce::JSON::parse(cliStdout);
    const auto* root = envelope.getDynamicObject();
    if (root == nullptr)
    {
        error = "Claude returned an unreadable response.";
        return {};
    }
    if (static_cast<bool>(root->getProperty("is_error")))
    {
        const auto message = root->getProperty("result").toString().trim();
        error = message.isEmpty() ? juce::String("Claude reported an error.") : message.substring(0, 300);
        return {};
    }
    auto answer = root->getProperty("structured_output");
    if (answer.getDynamicObject() == nullptr)
    {
        // Fallback: JSON inside the text result (possibly fenced).
        const auto text = root->getProperty("result").toString();
        const int open = text.indexOfChar('{'), close = text.lastIndexOfChar('}');
        if (open >= 0 && close > open)
            answer = juce::JSON::parse(text.substring(open, close + 1));
    }
    if (answer.getDynamicObject() == nullptr)
        error = "Claude's answer was not in the expected format.";
    return answer;
}

bool isNumberVar(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

int toClampedInt(const juce::var& v)
{
    const double d = static_cast<double>(v);
    return std::isfinite(d) ? static_cast<int>(std::clamp(d, -1.0e6, 1.0e6)) : 0;
}

// Untrusted notes -> valid Pattern: times clamped to the loop, pitches folded
// into range by octaves, same-pitch overlaps trimmed, capacity capped.
// `accepted` counts well-formed input notes.
Pattern sanitizeNotes(const juce::Array<juce::var>& notes, int& accepted)
{
    accepted = 0;
    std::vector<Note> candidates;
    for (const auto& item : notes)
    {
        const auto* note = item.getDynamicObject();
        if (note == nullptr)
            continue;
        const auto start = note->getProperty("start"), duration = note->getProperty("duration");
        const auto pitch = note->getProperty("pitch"), velocity = note->getProperty("velocity");
        if (!isNumberVar(start) || !isNumberVar(duration) || !isNumberVar(pitch) || !isNumberVar(velocity))
            continue;
        Note n;
        n.start = std::clamp(toClampedInt(start), 0, patternTicks - 1);
        n.duration = std::clamp(toClampedInt(duration), 30, patternTicks - n.start);
        n.pitch = foldPitch(std::clamp(toClampedInt(pitch), 0, 127));
        n.velocity = std::clamp(toClampedInt(velocity), 1, 127);
        candidates.push_back(n);
        ++accepted;
        if (candidates.size() >= 1024)
            break;
    }
    std::sort(candidates.begin(), candidates.end(), [](const Note& a, const Note& b) {
        return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch);
    });
    std::vector<Note> kept;
    for (const auto& note : candidates)
    {
        bool duplicate = false;
        for (auto& previous : kept)
            if (previous.pitch == note.pitch && previous.start + previous.duration > note.start)
            {
                if (previous.start == note.start)
                    duplicate = true;
                else
                    previous.duration = note.start - previous.start;
            }
        if (!duplicate)
            kept.push_back(note);
    }
    Pattern pattern;
    for (const auto& note : kept)
    {
        if (pattern.count >= Pattern::capacity)
            break;
        auto stored = note;
        stored.id = static_cast<std::uint32_t>(pattern.count + 1);
        pattern.notes[static_cast<std::size_t>(pattern.count++)] = stored;
    }
    return pattern;
}

// Untrusted drum hits -> valid DrumPattern. Out-of-range pads/steps are
// dropped (they have no sensible nearest value); velocity is clamped;
// duplicates keep the loudest.
DrumPattern sanitizeHits(const juce::Array<juce::var>& hits, int& accepted)
{
    accepted = 0;
    DrumPattern pattern;
    int seen = 0;
    for (const auto& item : hits)
    {
        if (++seen > 2048)
            break;
        const auto* hit = item.getDynamicObject();
        if (hit == nullptr)
            continue;
        const auto pad = hit->getProperty("pad"), step = hit->getProperty("step"), velocity = hit->getProperty("velocity");
        if (!isNumberVar(pad) || !isNumberVar(step) || !isNumberVar(velocity))
            continue;
        const int p = toClampedInt(pad), s = toClampedInt(step);
        if (p < 0 || p >= drumPads || s < 0 || s >= gridSteps)
            continue;
        auto& cell = pattern.steps[static_cast<std::size_t>(p)][static_cast<std::size_t>(s)];
        cell = static_cast<std::uint8_t>(std::max<int>(cell, std::clamp(toClampedInt(velocity), 1, 127)));
        ++accepted;
    }
    return pattern;
}
}

MelodyResult parseMelodyResponse(const juce::String& cliStdout)
{
    MelodyResult result;
    const auto answer = extractAnswer(cliStdout, result.error);
    if (result.error.isNotEmpty())
        return result;
    const auto* object = answer.getDynamicObject();
    const auto* notes = object->getProperty("notes").getArray();
    if (notes == nullptr)
    {
        result.error = "Claude's answer did not contain any notes.";
        return result;
    }
    result.title = cleanText(object->getProperty("title").toString(), 60);
    result.explanation = cleanText(object->getProperty("explanation").toString(), 400);
    int accepted = 0;
    const auto pattern = sanitizeNotes(*notes, accepted);
    if (pattern.count == 0 || !pattern.valid())
    {
        result.error = "Claude's melody could not be turned into valid notes.";
        return result;
    }
    result.pattern = pattern;
    return result;
}

MelodyResult generateMelody(const MelodyRequest& request, const std::atomic<bool>* cancel, int timeoutMs)
{
    MelodyResult result;
    const auto output = runSandboxedClaude(claudeArguments(), buildUserMessage(request), cancel, timeoutMs, result.error);
    if (result.error.isNotEmpty())
        return result;
    return parseMelodyResponse(output);
}

namespace
{
juce::String drumRulesText()
{
    juce::String text = "A drum pattern is a list of hits on 64 sixteenth-note steps: step 0 is bar 1 beat 1, 16 steps "
                        "per bar, 4 steps per beat (step 4 = beat 2, step 8 = beat 3). Pads:";
    for (int pad = 0; pad < drumPads; ++pad)
        text << (pad == 0 ? " " : ", ") << pad << " = " << drumNames[static_cast<std::size_t>(pad)];
    text << ". Each hit has pad, step (0-63), and velocity (1-127); use velocity for accents and ghost notes.";
    return text;
}

juce::String melodyRulesText()
{
    return "Melodic timing: 960 ticks per quarter note, one sixteenth = 240 ticks, one bar = 3840 ticks, loop "
           "length = 15360 ticks. Every note needs start >= 0, duration >= 60, start + duration <= 15360; prefer a "
           "120-tick grid. Pitch: full MIDI range 0 to 127; 60 is middle C. Notes on the same pitch must not "
           "overlap. Velocity 1-127. Chords are allowed.";
}

const juce::String noteItemSchema =
    R"({"type":"object","additionalProperties":false,"required":["start","duration","pitch","velocity"],"properties":{)"
    R"("start":{"type":"integer","minimum":0,"maximum":15359},"duration":{"type":"integer","minimum":1,"maximum":15360},)"
    R"("pitch":{"type":"integer","minimum":0,"maximum":127},"velocity":{"type":"integer","minimum":1,"maximum":127}}})";
const juce::String hitItemSchema =
    R"({"type":"object","additionalProperties":false,"required":["pad","step","velocity"],"properties":{)"
    R"("pad":{"type":"integer","minimum":0,"maximum":7},"step":{"type":"integer","minimum":0,"maximum":63},)"
    R"("velocity":{"type":"integer","minimum":1,"maximum":127}}})";
}

// ---- Sidebar assistant --------------------------------------------------

int assistantTargetSlot(const AssistantRequest& request)
{
    const auto t = static_cast<std::size_t>(std::clamp(request.track, 0, maxTracks - 1));
    const auto& song = request.project.song;
    if (request.part >= 0 && request.part < song.sections)
        return song.slots[static_cast<std::size_t>(request.part)][t];
    const bool drums = request.project.tracks[t].kind == TrackKind::Drums;
    return std::clamp(drums ? request.drumSlots[t] : request.melodySlots[t], 0, numPatterns - 1);
}

juce::String assistantSystemPrompt(bool drums)
{
    juce::String prompt =
        "You are the AI co-producer inside Sonora, a music production app, chatting with the producer in a "
        "sidebar. You work on one track at a time: the target track named in each message. You can hear the "
        "rest of the song through the notes and drum steps listed with each message.\n\n"
        "Write parts that complement the other tracks: same key and harmony, rhythm that locks with the "
        "groove, and space for the other parts. Follow the producer's requests and the conversation so far.\n\n"
        "You can see every loop (A-D) on every track, each labeled with its character in brackets — e.g. "
        "[sparse motif], [driving 16ths], [four-on-the-floor kick] — so loops that share a letter pattern can "
        "still be told apart: a sparse verse loop reads differently from a dense chorus one. Match the energy "
        "the producer asks for (a verse part wants something sparser than a chorus part).\n\n"
        "Reply in 1-3 short, friendly sentences (no markdown). If the producer asks for a new part or a change "
        "to the current one, set change=true and return the COMPLETE new pattern for the target track, "
        "including anything you keep unchanged (it replaces the current pattern). If they ask a question or "
        "nothing should change, set change=false and leave the pattern empty.\n\n"
        "Timing: 4/4, the loop is 4 bars.\n";
    if (drums)
    {
        prompt << "The target is a drum kit. " << drumRulesText() << " Return hits in the \"hits\" array.";
    }
    else
        prompt << "The target is a melodic instrument. " << melodyRulesText() << " Return notes in the \"notes\" array.";
    return prompt;
}

juce::String assistantSchema(bool drums)
{
    const juce::String items = drums ? R"("hits":{"type":"array","maxItems":512,"items":)" + hitItemSchema + "}"
                                     : R"("notes":{"type":"array","maxItems":256,"items":)" + noteItemSchema + "}";
    return R"({"type":"object","additionalProperties":false,"required":["reply","change",)"
           + juce::String(drums ? R"("hits"])" : R"("notes"])")
           + R"(,"properties":{"reply":{"type":"string","maxLength":600},"change":{"type":"boolean"},)" + items + "}}";
}

juce::String buildAssistantMessage(const AssistantRequest& request)
{
    const auto& project = request.project;
    const auto& song = project.song;
    const auto target = static_cast<std::size_t>(std::clamp(request.track, 0, maxTracks - 1));
    const bool inPart = request.part >= 0 && request.part < song.sections;
    juce::String message;
    message << "Song: " << juce::String(project.bpm, 0) << " BPM, 4/4. Song structure:";
    for (int s = 0; s < song.sections; ++s)
        message << (s == 0 ? " " : ", ") << (s + 1) << " " << songPartName(song.parts[static_cast<std::size_t>(s)])
                << (inPart && s == request.part ? " (current)" : "");
    message << ".\n";
    if (inPart)
        message << "Working on part " << (request.part + 1) << ", "
                << songPartName(song.parts[static_cast<std::size_t>(request.part)]) << " (bars "
                << (request.part * 4 + 1) << "-" << (request.part * 4 + 4) << ").\n\n";
    else
        message << "Working on a free 4-bar loop (not a specific song part).\n\n";

    juce::String context;
    for (int index = 0; index < maxTracks; ++index)
    {
        const auto i = static_cast<std::size_t>(index);
        const auto& track = project.tracks[i];
        if (track.kind == TrackKind::None || i == target)
            continue;
        const int slot = inPart ? song.slots[static_cast<std::size_t>(request.part)][i]
                                : (track.kind == TrackKind::Drums ? request.drumSlots[i] : request.melodySlots[i]);
        const bool silent = inPart && !song.trackOn[static_cast<std::size_t>(request.part)][i];
        context << "Track \"" << cleanText(track.trackName(), 60) << "\" (" << instrumentDescription(track) << ")"
                << (track.mix.mute ? " [muted]" : "") << (silent ? " [silent in this part]" : "") << ":\n";
        // Every loop, not just the current one: the producer's own patterns
        // are visible too, each labeled with its character (verse-like,
        // chorus-like...) so similar letters can still be told apart.
        for (int loop = 0; loop < numPatterns; ++loop)
        {
            const auto s = static_cast<std::size_t>(loop);
            context << "  Loop " << juce::String::charToString(static_cast<juce::juce_wchar>('A' + loop))
                    << (loop == std::clamp(slot, 0, numPatterns - 1) ? " (current)" : "")
                    << " [" << describeLoopRole(track, loop) << "]:\n";
            if (trackSlotHasContent(track, loop))
                context << (track.kind == TrackKind::Drums ? describeDrums(track.drumPatterns[s]) : describeNotes(track.melodies[s]));
        }
    }
    if (context.length() > maxContextChars)
        context = context.substring(0, maxContextChars) + "\n    (context truncated)\n";
    message << "Other tracks:\n" << (context.isEmpty() ? juce::String("    (none)\n") : context) << "\n";

    const auto& targetTrack = project.tracks[target];
    const auto slot = static_cast<std::size_t>(assistantTargetSlot(request));
    const bool drums = targetTrack.kind == TrackKind::Drums;
    message << "Target track \"" << cleanText(targetTrack.trackName(), 60) << "\" (" << instrumentDescription(targetTrack)
            << "). All of its loops (you are replacing loop "
            << juce::String::charToString(static_cast<juce::juce_wchar>('A' + static_cast<int>(slot)))
            << "; the others are context, do not rewrite them):\n";
    for (int loop = 0; loop < numPatterns; ++loop)
    {
        const auto s = static_cast<std::size_t>(loop);
        message << "  Loop " << juce::String::charToString(static_cast<juce::juce_wchar>('A' + loop))
                << (s == slot ? " (target)" : "") << " [" << describeLoopRole(targetTrack, loop) << "]:\n";
        if (trackSlotHasContent(targetTrack, loop))
            message << (drums ? describeDrums(targetTrack.drumPatterns[s]) : describeNotes(targetTrack.melodies[s]));
    }
    message << "\n";

    const auto first = request.history.size() > static_cast<std::size_t>(maxHistoryTurns)
        ? request.history.size() - static_cast<std::size_t>(maxHistoryTurns) : 0;
    if (first < request.history.size())
    {
        message << "Conversation so far:\n";
        for (auto i = first; i < request.history.size(); ++i)
            message << (request.history[i].fromUser ? "Producer: " : "You: ")
                    << cleanText(request.history[i].text, maxTurnChars).replace("\n", " ") << "\n";
        message << "\n";
    }
    message << "The producer says:\n\"\"\"\n" << cleanText(request.message, maxPromptChars) << "\n\"\"\"\n";
    return message;
}

juce::StringArray assistantArguments(bool drums)
{
    return sandboxArguments(assistantSystemPrompt(drums), assistantSchema(drums));
}

AssistantResult parseAssistantResponse(const juce::String& cliStdout, bool drums)
{
    AssistantResult result;
    result.drums = drums;
    const auto answer = extractAnswer(cliStdout, result.error);
    if (result.error.isNotEmpty())
        return result;
    const auto* object = answer.getDynamicObject();
    result.reply = cleanText(object->getProperty("reply").toString(), 600);
    result.changed = static_cast<bool>(object->getProperty("change"));
    if (!result.changed)
    {
        if (result.reply.isEmpty())
            result.error = "Claude sent an empty reply.";
        return result;
    }
    const auto* items = object->getProperty(drums ? "hits" : "notes").getArray();
    if (items == nullptr)
    {
        result.error = drums ? "Claude said it changed the beat but sent no hits." : "Claude said it changed the part but sent no notes.";
        return result;
    }
    int accepted = 0;
    if (drums)
        result.drumPattern = sanitizeHits(*items, accepted);
    else
        result.pattern = sanitizeNotes(*items, accepted);
    // An empty list is a deliberate "clear it"; a non-empty list that yields
    // nothing usable is an error.
    if (!items->isEmpty() && result.count() == 0)
        result.error = drums ? "Claude's beat could not be turned into valid drum hits."
                             : "Claude's part could not be turned into valid notes.";
    if (result.reply.isEmpty())
        result.reply = drums ? "Updated the beat." : "Updated the part.";
    return result;
}

AssistantResult runAssistant(const AssistantRequest& request, const std::atomic<bool>* cancel, int timeoutMs)
{
    const auto t = static_cast<std::size_t>(std::clamp(request.track, 0, maxTracks - 1));
    const bool drums = request.project.tracks[t].kind == TrackKind::Drums;
    AssistantResult result;
    result.drums = drums;
    if (request.project.tracks[t].kind == TrackKind::None)
    {
        result.error = "Select an instrument or drum track first.";
        return result;
    }
    const auto output = runSandboxedClaude(assistantArguments(drums), buildAssistantMessage(request), cancel,
                                           timeoutMs, result.error);
    if (result.error.isNotEmpty())
        return result;
    return parseAssistantResponse(output, drums);
}

// ---- Song composer ------------------------------------------------------

namespace
{
constexpr std::array<SongPart, 10> composerParts { SongPart::Intro, SongPart::Verse, SongPart::PreChorus, SongPart::Chorus,
                                                   SongPart::Bridge, SongPart::Break, SongPart::Build, SongPart::Drop,
                                                   SongPart::Outro, SongPart::Section };

SongPart partFromName(const juce::String& name)
{
    for (const auto part : composerParts)
        if (name.trim().equalsIgnoreCase(songPartName(part)))
            return part;
    return SongPart::Section;
}

juce::String letter(int slot) { return juce::String::charToString(static_cast<juce::juce_wchar>('A' + slot)); }
}

juce::String songSystemPrompt()
{
    juce::String prompt =
        "You are the AI co-producer inside Sonora, a music production app, arranging the producer's 4-bar loops "
        "into a complete song. Every section is 4 bars. For each section you choose its type (Intro, Verse, "
        "Pre-Chorus, Chorus, Bridge, Break, Build, Drop, Outro) and, for every track, which of its loops (A-D) "
        "plays or whether it is silent.\n\n"
        "Make it feel like a real, evolving song: a clear shape (typically 8-14 sections, at most 16), energy that "
        "builds into choruses, contrast between sections (fewer tracks in intros, verses, and breaks; the full band "
        "in choruses; drums out or thinned for breaks and outros), and a strong final chorus.\n\n"
        "Every loop carries a character label in brackets — [sparse motif], [driving 16ths], [four-on-the-floor "
        "kick], and so on. Use it to match loops to sections: sparse, low-energy loops for verses, intros, and "
        "breaks; the densest loops for choruses and drops; the runner-up for builds, pre-choruses, and bridges.\n\n"
        "Add variation by writing NEW loops into EMPTY loop slots only (never rewrite a loop that already has "
        "content): for example a drum loop with a fill at the end of bar 4 to lead into a chorus, a busier or "
        "half-time groove, a stripped-down bass for a bridge, or a lifted or answering melody for the last "
        "chorus. Keep new loops in the same key, harmony, and groove as the existing ones, and use them in the "
        "arrangement. " + melodyRulesText() + " " + drumRulesText() + " Melodic tracks get \"notes\"; drum "
        "tracks get \"hits\".\n\n"
        "Reply in 2-4 short, friendly sentences (no markdown) describing the song's shape and the variations you "
        "added. If the producer asks a question or nothing should change, set change=false with empty sections "
        "and newLoops. When change=true, sections is the COMPLETE new arrangement (it replaces the current one).";
    return prompt;
}

juce::String songSchema()
{
    juce::StringArray partNames;
    for (const auto part : composerParts)
        partNames.add("\"" + juce::String(songPartName(part)) + "\"");
    const juce::String loopEnum = R"({"type":"string","enum":["A","B","C","D"]})";
    return R"({"type":"object","additionalProperties":false,"required":["reply","change","sections","newLoops"],"properties":{)"
           R"("reply":{"type":"string","maxLength":800},"change":{"type":"boolean"},)"
           R"("sections":{"type":"array","maxItems":16,"items":{"type":"object","additionalProperties":false,)"
           R"("required":["part","tracks"],"properties":{"part":{"type":"string","enum":[)" + partNames.joinIntoString(",") + R"(]},)"
           R"("tracks":{"type":"array","maxItems":8,"items":{"type":"object","additionalProperties":false,)"
           R"("required":["track","loop"],"properties":{"track":{"type":"integer","minimum":0,"maximum":7},"loop":)" + loopEnum + "}}}}}},"
           R"("newLoops":{"type":"array","maxItems":12,"items":{"type":"object","additionalProperties":false,)"
           R"("required":["track","loop"],"properties":{"track":{"type":"integer","minimum":0,"maximum":7},"loop":)" + loopEnum + ","
           R"("notes":{"type":"array","maxItems":256,"items":)" + noteItemSchema + "},"
           R"("hits":{"type":"array","maxItems":512,"items":)" + hitItemSchema + "}}}}}}";
}

juce::String buildSongMessage(const SongRequest& request)
{
    const auto& project = request.project;
    const auto& song = project.song;
    juce::String tracks;
    for (int index = 0; index < maxTracks; ++index)
    {
        const auto& track = project.tracks[static_cast<std::size_t>(index)];
        if (track.kind == TrackKind::None)
            continue;
        tracks << "Track " << index << " \"" << cleanText(track.trackName(), 60) << "\" (" << instrumentDescription(track) << ")"
               << (track.mix.mute ? " [muted]" : "") << ":\n";
        for (int slot = 0; slot < numPatterns; ++slot)
        {
            const auto s = static_cast<std::size_t>(slot);
            const bool empty = !trackSlotHasContent(track, slot);
            tracks << "  Loop " << letter(slot) << " [" << describeLoopRole(track, slot) << "]"
                   << (empty ? ": (empty, free for a new loop)\n" : ":\n");
            if (!empty)
                tracks << (track.kind == TrackKind::Drums ? describeDrums(track.drumPatterns[s]) : describeNotes(track.melodies[s]));
        }
    }
    if (tracks.length() > 2 * maxContextChars)
        tracks = tracks.substring(0, 2 * maxContextChars) + "\n  (context truncated)\n";
    juce::String message;
    message << "Song: " << juce::String(project.bpm, 0) << " BPM, 4/4, sections of 4 bars.\n\n" << tracks << "\n";
    message << "Current arrangement (" << song.sections << " sections):\n";
    for (int s = 0; s < song.sections; ++s)
    {
        const auto si = static_cast<std::size_t>(s);
        juce::StringArray playing;
        for (int index = 0; index < maxTracks; ++index)
        {
            const auto i = static_cast<std::size_t>(index);
            if (project.tracks[i].kind != TrackKind::None && song.trackOn[si][i])
                playing.add("track " + juce::String(index) + " loop " + letter(song.slots[si][i]));
        }
        message << "  " << (s + 1) << ". " << songPartName(song.parts[si]) << ": "
                << (playing.isEmpty() ? juce::String("(all silent)") : playing.joinIntoString(", ")) << "\n";
    }
    message << "\n";
    const auto first = request.history.size() > static_cast<std::size_t>(maxHistoryTurns)
        ? request.history.size() - static_cast<std::size_t>(maxHistoryTurns) : 0;
    if (first < request.history.size())
    {
        message << "Conversation so far:\n";
        for (auto i = first; i < request.history.size(); ++i)
            message << (request.history[i].fromUser ? "Producer: " : "You: ")
                    << cleanText(request.history[i].text, maxTurnChars).replace("\n", " ") << "\n";
        message << "\n";
    }
    message << "The producer says:\n\"\"\"\n" << cleanText(request.message, maxPromptChars) << "\n\"\"\"\n";
    return message;
}

juce::StringArray songArguments() { return sandboxArguments(songSystemPrompt(), songSchema()); }

SongResult parseSongResponse(const juce::String& cliStdout, const ProjectState& project)
{
    SongResult result;
    const auto answer = extractAnswer(cliStdout, result.error);
    if (result.error.isNotEmpty())
        return result;
    const auto* object = answer.getDynamicObject();
    result.reply = cleanText(object->getProperty("reply").toString(), 800);
    result.changed = static_cast<bool>(object->getProperty("change"));
    if (!result.changed)
    {
        if (result.reply.isEmpty())
            result.error = "Claude sent an empty reply.";
        return result;
    }
    auto isNumber = [](const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); };
    auto trackIndex = [&](const juce::var& v) {
        if (!isNumber(v))
            return -1;
        const int index = toClampedInt(v);
        return index >= 0 && index < maxTracks && project.tracks[static_cast<std::size_t>(index)].kind != TrackKind::None
            ? index : -1;
    };
    auto slotIndex = [](const juce::var& v) {
        const auto text = v.toString().trim().toUpperCase();
        return text.length() == 1 && text[0] >= 'A' && text[0] < 'A' + numPatterns ? static_cast<int>(text[0] - 'A') : -1;
    };

    // New loops: only into slots that are empty in the project right now.
    std::array<std::array<bool, numPatterns>, maxTracks> filled {};
    for (int index = 0; index < maxTracks; ++index)
        for (int slot = 0; slot < numPatterns; ++slot)
            filled[static_cast<std::size_t>(index)][static_cast<std::size_t>(slot)]
                = trackSlotHasContent(project.tracks[static_cast<std::size_t>(index)], slot);
    if (const auto* loops = object->getProperty("newLoops").getArray())
        for (const auto& item : *loops)
        {
            const auto* loop = item.getDynamicObject();
            if (loop == nullptr)
                continue;
            const int track = trackIndex(loop->getProperty("track"));
            const int slot = slotIndex(loop->getProperty("loop"));
            if (track < 0 || slot < 0)
                continue;
            const auto& target = project.tracks[static_cast<std::size_t>(track)];
            if (trackSlotHasContent(target, slot))
            {
                result.skipped.add("kept " + target.trackName() + " loop " + letter(slot) + " (it already had content)");
                continue;
            }
            SongLoopWrite write;
            write.track = track;
            write.slot = slot;
            write.drums = target.kind == TrackKind::Drums;
            int accepted = 0;
            if (const auto* items = loop->getProperty(write.drums ? "hits" : "notes").getArray())
            {
                if (write.drums)
                    write.drumPattern = sanitizeHits(*items, accepted);
                else
                    write.pattern = sanitizeNotes(*items, accepted);
            }
            const int count = write.drums ? write.drumPattern.hitCount() : write.pattern.count;
            if (count == 0)
                continue;
            // A second write to the same slot replaces the first.
            result.writes.erase(std::remove_if(result.writes.begin(), result.writes.end(),
                                               [&](const SongLoopWrite& w) { return w.track == track && w.slot == slot; }),
                                result.writes.end());
            result.writes.push_back(write);
            filled[static_cast<std::size_t>(track)][static_cast<std::size_t>(slot)] = true;
        }

    // Arrangement: tracks not listed in a section are silent there; cells that
    // point at a loop with no content (even after new loops) are switched off.
    const auto* sections = object->getProperty("sections").getArray();
    if (sections == nullptr || sections->isEmpty())
    {
        result.error = "Claude did not send an arrangement.";
        return result;
    }
    Arrangement song;
    song.sections = 0;
    for (const auto& item : *sections)
    {
        if (song.sections >= maxSections)
            break;
        const auto* section = item.getDynamicObject();
        if (section == nullptr)
            continue;
        const auto s = static_cast<std::size_t>(song.sections);
        song.parts[s] = partFromName(section->getProperty("part").toString());
        song.trackOn[s].fill(false);
        song.slots[s].fill(0);
        if (const auto* tracksInSection = section->getProperty("tracks").getArray())
            for (const auto& entry : *tracksInSection)
            {
                const auto* cell = entry.getDynamicObject();
                if (cell == nullptr)
                    continue;
                const int track = trackIndex(cell->getProperty("track"));
                const int slot = slotIndex(cell->getProperty("loop"));
                if (track < 0 || slot < 0 || !filled[static_cast<std::size_t>(track)][static_cast<std::size_t>(slot)])
                    continue;
                song.slots[s][static_cast<std::size_t>(track)] = static_cast<std::uint8_t>(slot);
                song.trackOn[s][static_cast<std::size_t>(track)] = true;
            }
        ++song.sections;
    }
    if (song.sections == 0 || !song.valid())
    {
        result.error = "Claude's arrangement could not be used.";
        return result;
    }
    result.song = song;
    if (result.reply.isEmpty())
        result.reply = "Here's a full arrangement.";
    return result;
}

bool applySongResult(ProjectState& project, const SongResult& result)
{
    if (!result.ok() || !result.changed)
        return false;
    for (const auto& write : result.writes)
    {
        auto& track = project.tracks[static_cast<std::size_t>(write.track)];
        const auto slot = static_cast<std::size_t>(write.slot);
        // Re-check at apply time: never overwrite content the producer added meanwhile.
        if ((track.kind == TrackKind::Drums) != write.drums || track.kind == TrackKind::None
            || trackSlotHasContent(track, write.slot))
            continue;
        if (write.drums)
            track.drumPatterns[slot] = write.drumPattern;
        else
            track.melodies[slot] = write.pattern;
    }
    project.song = result.song;
    return true;
}

SongResult runSongComposer(const SongRequest& request, const std::atomic<bool>* cancel, int timeoutMs)
{
    SongResult result;
    bool anyTrack = false;
    for (const auto& track : request.project.tracks)
        anyTrack = anyTrack || track.kind != TrackKind::None;
    if (!anyTrack)
    {
        result.error = "Add a track with some loops first.";
        return result;
    }
    const auto output = runSandboxedClaude(songArguments(), buildSongMessage(request), cancel, timeoutMs, result.error);
    if (result.error.isNotEmpty())
        return result;
    return parseSongResponse(output, request.project);
}
}
