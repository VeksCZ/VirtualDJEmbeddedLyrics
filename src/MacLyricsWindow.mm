#include "MacLyricsWindow.hpp"
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

#include "LrcShift.hpp"
#include "LrclibLookup.hpp"
#include "Lyrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {
NSString* NS(const std::wstring& value) {
    return [NSString stringWithUTF8String:LrclibUtf8(value).c_str()] ?: @"";
}

std::wstring W(NSString* value) {
    return value ? LrclibWiden(std::string([value UTF8String] ?: "")) : std::wstring{};
}

std::wstring FormatDocument(const LyricsDocument& document, bool timed) {
    std::wstring result;
    for (const auto& line : document.lines) {
        if (!result.empty()) result += L"\n";
        if (timed) {
            const long long total = std::max<long long>(0, line.timeMs);
            wchar_t stamp[32]{};
            std::swprintf(stamp, 32, L"[%02lld:%02lld.%02lld] ", total / 60000, (total / 1000) % 60,
                          (total % 1000) / 10);
            result += stamp;
        }
        result += line.text;
    }
    return result;
}

size_t CountLines(const std::wstring& text) {
    size_t count = 0;
    std::wistringstream lines(text);
    for (std::wstring line; std::getline(lines, line);)
        if (line.find_first_not_of(L" \t\r") != std::wstring::npos) ++count;
    return count;
}

std::wstring Describe(const std::wstring& text) {
    const auto lines = CountLines(text);
    return lines ? L"yes (" + std::to_wstring(lines) + L" lines)" : std::wstring(L"no");
}

std::wstring FormatDuration(double seconds) {
    if (seconds <= 0) return {};
    const long total = std::lround(seconds);
    wchar_t buffer[16]{};
    std::swprintf(buffer, 16, L"%ld:%02ld", total / 60, total % 60);
    return buffer;
}

std::filesystem::path TempFile(const char* stem, const char* extension) {
    const char* dir = std::getenv("TMPDIR");
    std::filesystem::path base = dir && *dir ? dir : "/tmp";
    return base / (std::string(stem) + "-" + std::to_string(getpid()) + "-" +
                   std::to_string(static_cast<long long>([NSDate date].timeIntervalSince1970 * 1000)) +
                   extension);
}

// Runs the helper and returns its exit status, or -1 if it could not start.
int RunHelper(const std::filesystem::path& helper, NSArray<NSString*>* arguments) {
    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:helper.c_str()]];
    task.arguments = arguments;
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    NSError* error = nil;
    if (![task launchAndReturnError:&error]) return -1;
    [task waitUntilExit];
    return task.terminationStatus;
}

struct LookupResult {
    std::map<std::wstring, std::wstring> fields;
    std::wstring text;
    std::wstring failure;
};

LookupResult RunHelperLookup(const MacLyricsRequest& request) {
    LookupResult result;
    const auto output = TempFile("LrcLookup", ".txt");
    const int status = RunHelper(request.helper, @[@"--lookup-lyrics",
        [NSString stringWithUTF8String:request.audioPath.c_str()],
        [NSString stringWithUTF8String:output.c_str()]]);
    if (status < 0) { result.failure = L"LrcHelper could not be started."; return result; }
    std::ifstream input(output, std::ios::binary);
    std::ostringstream buffer; buffer << input.rdbuf();
    input.close();
    std::error_code error;
    std::filesystem::remove(output, error);
    std::string raw = buffer.str();
    if (raw.empty()) { result.failure = L"LrcHelper returned no answer."; return result; }
    const auto split = raw.find("\n\n");
    if (split != std::string::npos) result.text = LrclibWiden(raw.substr(split + 2));
    std::istringstream lines(raw.substr(0, split));
    for (std::string line; std::getline(lines, line);) {
        const auto equals = line.find('=');
        if (equals != std::string::npos)
            result.fields[LrclibWiden(line.substr(0, equals))] = LrclibWiden(line.substr(equals + 1));
    }
    return result;
}

LookupResult RunBuiltInLookup(const MacLyricsRequest& request) {
    LookupResult result;
    const auto tags = ReadBasicTrackTags(request.audioPath);
    std::wstring query = tags.artist + (tags.artist.empty() ? L"" : L" - ") + tags.title;
    if (request.durationSeconds > 0) query += L" (" + FormatDuration(request.durationSeconds) + L")";
    result.fields[L"query"] = query;
    const auto match = LookupLrclib(tags.artist, tags.title, request.durationSeconds);
    if (match.found) {
        result.fields[L"status"] = L"found";
        result.fields[L"source"] = match.source;
        result.fields[L"synchronized"] = match.synchronized ? L"1" : L"0";
        result.fields[L"matched"] = match.matched;
        result.text = match.text;
    } else {
        result.fields[L"status"] = match.error.empty() ? L"not_found" : L"error";
        result.fields[L"message"] = match.error.empty() ? L"LRCLIB: not found"
                                                        : L"LRCLIB unavailable (" + match.error + L")";
    }
    result.fields[L"message"] += std::wstring(result.fields[L"message"].empty() ? L"" : L"; ") +
        L"TIDAL is searched only when the plugin is installed by LyricsTools";
    return result;
}

std::wstring Field(const LookupResult& r, const wchar_t* key) {
    const auto it = r.fields.find(key);
    return it == r.fields.end() ? std::wstring{} : it->second;
}
}  // namespace

@interface LRCVDJLyricsWindowController : NSObject <NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSPopUpButton* mode;
@property(nonatomic, strong) NSTextView* editor;
@property(nonatomic, strong) NSTextField* status;
@property(nonatomic, strong) NSButton* searchButton;
@property(nonatomic, strong) NSButton* saveButton;
@property(nonatomic, strong) NSTextField* startAt;
@property(nonatomic) BOOL saved;
@property(nonatomic) BOOL busy;
@end

@implementation LRCVDJLyricsWindowController {
    MacLyricsRequest _request;
    std::wstring _timed;
    std::wstring _untimed;
    NSInteger _shownMode;
}

- (instancetype)initWithRequest:(const MacLyricsRequest&)request {
    if ((self = [super init])) {
        _request = request;
        _timed = FormatDocument(LoadEmbeddedTimedLyrics(request.audioPath).document, true);
        _untimed = FormatDocument(LoadEmbeddedUntimedLyrics(request.audioPath).document, false);
        [self build];
    }
    return self;
}

- (NSTextField*)label:(NSString*)text frame:(NSRect)frame {
    NSTextField* field = [NSTextField wrappingLabelWithString:text];
    field.frame = frame;
    field.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self.window.contentView addSubview:field];
    return field;
}

- (void)build {
    const NSRect frame = NSMakeRect(0, 0, 760, 660);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                backing:NSBackingStoreBuffered defer:NO];
    const auto stem = _request.audioPath.stem().wstring();
    self.window.title = [@"LRC lyrics - " stringByAppendingString:NS(stem)];
    self.window.delegate = self;
    self.window.releasedWhenClosed = NO;
    NSView* content = self.window.contentView;

    const auto tags = ReadBasicTrackTags(_request.audioPath);
    const auto song = tags.title.empty() ? stem : tags.artist + L" - " + tags.title;
    [self label:[@"Song: " stringByAppendingString:NS(song)] frame:NSMakeRect(16, 626, 728, 18)];
    [self label:[@"File: " stringByAppendingString:NS(_request.audioPath.wstring())]
          frame:NSMakeRect(16, 604, 728, 18)];
    [self label:NS(L"In the MP3 now:   Timed: " + Describe(_timed) + L"     Untimed: " + Describe(_untimed))
          frame:NSMakeRect(16, 582, 728, 18)];

    NSBox* online = [[NSBox alloc] initWithFrame:NSMakeRect(12, 462, 736, 112)];
    online.title = @"Find online";
    [content addSubview:online];
    self.searchButton = [NSButton buttonWithTitle:@"Search TIDAL / LRCLIB" target:self action:@selector(search:)];
    self.searchButton.frame = NSMakeRect(26, 518, 190, 30);
    [content addSubview:self.searchButton];
    self.status = [self label:@"Uses the song's ISRC, artist, title and length. Found lyrics go into the editor; "
                               "nothing is saved until you press Save."
                        frame:NSMakeRect(228, 472, 506, 78)];
    self.status.lineBreakMode = NSLineBreakByWordWrapping;

    NSBox* lyrics = [[NSBox alloc] initWithFrame:NSMakeRect(12, 56, 736, 400)];
    lyrics.title = @"Lyrics";
    [content addSubview:lyrics];
    [self label:@"Type" frame:NSMakeRect(26, 410, 40, 18)];
    self.mode = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(66, 404, 220, 26) pullsDown:NO];
    [self.mode addItemsWithTitles:@[@"Timed (LRC timestamps)", @"Untimed"]];
    [self.mode setTarget:self];
    [self.mode setAction:@selector(modeChanged:)];
    [content addSubview:self.mode];
    [self label:@"Timed rows use [mm:ss.xx] text. Each type keeps its own text; paste or type here."
          frame:NSMakeRect(296, 408, 440, 18)];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(26, 70, 708, 326)];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    self.editor = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 708, 326)];
    self.editor.richText = NO;
    self.editor.automaticQuoteSubstitutionEnabled = NO;
    self.editor.automaticDashSubstitutionEnabled = NO;
    self.editor.automaticTextReplacementEnabled = NO;
    self.editor.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    self.editor.autoresizingMask = NSViewWidthSizable;
    scroll.documentView = self.editor;
    [content addSubview:scroll];

    // Timing fix: move every timestamp so the first line starts where the vocals really start.
    [self label:@"First line at" frame:NSMakeRect(16, 20, 84, 18)];
    self.startAt = [[NSTextField alloc] initWithFrame:NSMakeRect(102, 16, 80, 24)];
    self.startAt.stringValue = _request.positionSeconds >= 0
        ? NS(FormatLyricsTime(static_cast<std::int64_t>(_request.positionSeconds * 1000.0))) : @"";
    self.startAt.placeholderString = @"0:23.40";
    [content addSubview:self.startAt];
    NSButton* shift = [NSButton buttonWithTitle:@"Shift timing" target:self action:@selector(shiftTiming:)];
    shift.frame = NSMakeRect(188, 14, 120, 30);
    [content addSubview:shift];

    self.saveButton = [NSButton buttonWithTitle:@"Save to MP3 tag" target:self action:@selector(save:)];
    self.saveButton.frame = NSMakeRect(500, 14, 150, 30);
    self.saveButton.keyEquivalent = @"";
    [content addSubview:self.saveButton];
    NSButton* cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancel:)];
    cancel.frame = NSMakeRect(656, 14, 92, 30);
    cancel.keyEquivalent = @"\033";
    [content addSubview:cancel];

    const bool timed = !_timed.empty() || _untimed.empty();
    [self.mode selectItemAtIndex:timed ? 0 : 1];
    _shownMode = self.mode.indexOfSelectedItem;
    [self show];
    [self.window center];
}

- (void)store {
    (_shownMode == 0 ? _timed : _untimed) = W(self.editor.string);
}

- (void)show {
    self.editor.string = NS(self.mode.indexOfSelectedItem == 0 ? _timed : _untimed);
    _shownMode = self.mode.indexOfSelectedItem;
}

- (void)modeChanged:(id)sender {
    (void)sender;
    [self store];
    [self show];
}

- (void)setBusy:(BOOL)busy {
    _busy = busy;
    self.searchButton.enabled = !busy;
    self.saveButton.enabled = !busy;
}

- (void)search:(id)sender {
    (void)sender;
    if (self.busy) return;
    self.busy = YES;
    const bool helper = !_request.helper.empty();
    self.status.stringValue = helper ? @"Searching TIDAL, then LRCLIB..." : @"Searching LRCLIB...";
    const MacLyricsRequest request = _request;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        LookupResult result = helper ? RunHelperLookup(request) : RunBuiltInLookup(request);
        if (helper && !result.failure.empty()) {
            const auto failure = result.failure;
            result = RunBuiltInLookup(request);
            result.fields[L"message"] = failure + L" " + result.fields[L"message"];
        }
        dispatch_async(dispatch_get_main_queue(), ^{ [self finishSearch:result]; });
    });
}

- (void)finishSearch:(const LookupResult&)result {
    self.busy = NO;
    if (!result.failure.empty()) { self.status.stringValue = NS(result.failure); return; }
    const auto status = Field(result, L"status");
    const auto message = Field(result, L"message");
    const auto query = Field(result, L"query");
    if (status != L"found" || result.text.empty()) {
        std::wstring text = status == L"login_required"
            ? L"Not found. TIDAL is not signed in: sign in once in LyricsTools (Online tab)."
            : status == L"error" ? L"Search failed." : L"No lyrics found online.";
        if (!message.empty()) text += L"\n" + message;
        if (!query.empty()) text += L"\nLooked for: " + query;
        self.status.stringValue = NS(text);
        return;
    }
    const bool timed = Field(result, L"synchronized") == L"1";
    std::wstring lyrics;
    for (wchar_t c : result.text) if (c != L'\r') lyrics += c;
    [self store];
    auto& target = timed ? _timed : _untimed;
    if (!target.empty() && target != lyrics) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Lyrics found";
        alert.informativeText = [NSString stringWithFormat:@"Replace the current %@ text in the editor "
                                                           "with the lyrics found online?", timed ? @"Timed" : @"Untimed"];
        [alert addButtonWithTitle:@"Replace"];
        [alert addButtonWithTitle:@"Keep"];
        if ([alert runModal] != NSAlertFirstButtonReturn) {
            self.status.stringValue = @"Found lyrics were not used.";
            return;
        }
    }
    target = lyrics;
    [self.mode selectItemAtIndex:timed ? 0 : 1];
    [self show];
    std::wstring text = L"Found on " + Field(result, L"source") + L"  -  " +
        (timed ? L"Timed (synchronized) lyrics, " : L"Untimed (plain) lyrics, ") +
        std::to_wstring(CountLines(lyrics)) + L" lines.";
    if (const auto matched = Field(result, L"matched"); !matched.empty()) text += L"\nMatched: " + matched;
    if (!query.empty()) text += L"\nLooked for: " + query;
    text += L"\nLoaded into the editor below. Check it, then Save to MP3 tag.";
    self.status.stringValue = NS(text);
}

- (void)shiftTiming:(id)sender {
    (void)sender;
    if (self.busy) return;
    const auto target = ParseLyricsTime(W(self.startAt.stringValue));
    if (target < 0) {
        self.status.stringValue = @"Enter the time where the first line starts, e.g. 0:23.40 (the current "
                                   "playback position is filled in when the window opens).";
        return;
    }
    [self store];
    if (self.mode.indexOfSelectedItem != 0) {
        [self.mode selectItemAtIndex:0];
        [self show];
    }
    auto text = _timed;
    std::int64_t delta = 0;
    if (!ShiftLrcTimestamps(text, target, &delta)) {
        self.status.stringValue = @"The Timed text has no [mm:ss.xx] timestamps to shift.";
        return;
    }
    _timed = text;
    [self show];
    self.status.stringValue = [NSString stringWithFormat:@"All timestamps moved by %+.2f s; first line now at %@. "
                                                         "Save to MP3 tag to keep it.",
                               static_cast<double>(delta) / 1000.0, NS(FormatLyricsTime(target))];
}

- (void)save:(id)sender {
    (void)sender;
    if (self.busy) return;
    [self store];
    const bool timed = self.mode.indexOfSelectedItem == 0;
    const auto text = timed ? _timed : _untimed;
    if (CountLines(text) == 0) { [self cancel:nil]; return; }
    if (_request.helper.empty()) {
        self.status.stringValue = @"Saving needs LrcHelper. Install or update the plugin from LyricsTools.";
        return;
    }
    const auto pending = TempFile("LrcEditor", ".lyrics");
    {
        std::ofstream output(pending, std::ios::binary | std::ios::trunc);
        output << LrclibUtf8(text);
    }
    // Same validation as Windows: classify by content, refuse empty/garbage text.
    const auto validation = LoadPlainTextLyrics(pending);
    if (validation.document.empty()) {
        std::error_code error;
        std::filesystem::remove(pending, error);
        self.status.stringValue = timed ? @"Timed lyrics must contain LRC timestamps such as [01:23.45] Text."
                                        : @"Untimed lyrics must contain at least one non-empty line.";
        return;
    }
    const bool synchronized = validation.document.synchronized;
    self.busy = YES;
    self.status.stringValue = @"Saving to the MP3...";
    const MacLyricsRequest request = _request;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        const int status = RunHelper(request.helper, @[@"--write-editor-text",
            [NSString stringWithUTF8String:request.audioPath.c_str()],
            [NSString stringWithUTF8String:pending.c_str()], synchronized ? @"timed" : @"untimed"]);
        std::error_code error;
        std::filesystem::remove(pending, error);
        dispatch_async(dispatch_get_main_queue(), ^{
            self.busy = NO;
            if (status != 0) {
                self.status.stringValue = [NSString stringWithFormat:@"Saving failed (LrcHelper exit code %d).", status];
                return;
            }
            self.saved = YES;
            [NSApp stopModal];
        });
    });
}

- (void)cancel:(id)sender {
    (void)sender;
    if (self.busy) return;
    [NSApp stopModal];
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    if (self.busy) return NO;
    [NSApp stopModal];
    return YES;
}
@end

bool ShowMacLyricsWindow(const MacLyricsRequest& request) {
    @autoreleasepool {
        LRCVDJLyricsWindowController* controller = [[LRCVDJLyricsWindowController alloc] initWithRequest:request];
        [controller.window makeKeyAndOrderFront:nil];
        [NSApp runModalForWindow:controller.window];
        [controller.window orderOut:nil];
        return controller.saved;
    }
}
#endif
