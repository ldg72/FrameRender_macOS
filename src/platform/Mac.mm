#include "platform/Platform.hpp"
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <cstdlib>

namespace fr::platform {
void initialize(){
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    NSImage* icon=[[NSImage alloc]initWithSize:NSMakeSize(256,256)];
    [icon lockFocus];
    [[NSColor colorWithSRGBRed:.09 green:.10 blue:.12 alpha:1]setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(0,0,256,256) xRadius:52 yRadius:52]fill];
    [[NSColor colorWithSRGBRed:.32 green:.82 blue:.69 alpha:1]setStroke];
    NSBezierPath* frame=[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(45,50,166,156) xRadius:15 yRadius:15];[frame setLineWidth:13];[frame stroke];
    [[NSColor colorWithSRGBRed:.32 green:.82 blue:.69 alpha:1]setFill];
    NSBezierPath* triangle=[NSBezierPath bezierPath];[triangle moveToPoint:NSMakePoint(105,89)];[triangle lineToPoint:NSMakePoint(105,167)];[triangle lineToPoint:NSMakePoint(167,128)];[triangle closePath];[triangle fill];
    [icon unlockFocus];[NSApp setApplicationIconImage:icon];
}
std::vector<std::filesystem::path> openVideos(){
    @autoreleasepool{
        NSOpenPanel* panel=[NSOpenPanel openPanel];panel.title=@"Import media";panel.canChooseDirectories=NO;panel.allowsMultipleSelection=YES;
        panel.allowedContentTypes=@[UTTypeMovie,UTTypeVideo];panel.allowsOtherFileTypes=YES;
        std::vector<std::filesystem::path> result;
        if([panel runModal]==NSModalResponseOK)for(NSURL* url in panel.URLs)result.emplace_back(url.fileSystemRepresentation);
        return result;
    }
}
std::filesystem::path openProject(){
    @autoreleasepool{
        NSOpenPanel* panel=[NSOpenPanel openPanel];panel.title=@"Open FrameRender project";panel.canChooseDirectories=NO;
        UTType* type=[UTType typeWithFilenameExtension:@"frp"];panel.allowedContentTypes=@[type ? type : UTTypeData];panel.allowsOtherFileTypes=YES;
        if([panel runModal]==NSModalResponseOK)return panel.URL.fileSystemRepresentation;
        return {};
    }
}
static std::filesystem::path save(const std::string& name,NSString* extension,NSString* title){
    @autoreleasepool{
        NSSavePanel* panel=[NSSavePanel savePanel];panel.title=title;panel.canCreateDirectories=YES;
        UTType* type=[UTType typeWithFilenameExtension:extension];panel.allowedContentTypes=@[type ? type : UTTypeData];
        panel.nameFieldStringValue=[NSString stringWithUTF8String:name.c_str()];
        if([panel runModal]==NSModalResponseOK)return panel.URL.fileSystemRepresentation;
        return {};
    }
}
std::filesystem::path saveProject(const std::string& name){return save(name+".frp",@"frp",@"Save project");}
std::filesystem::path saveVideo(const std::string& name,const std::string& extension){return save(name+"."+extension,[NSString stringWithUTF8String:extension.c_str()],@"Export video");}
std::filesystem::path applicationSupport(){
    if(const char* overridePath=std::getenv("FRAMERENDER_CONFIG_DIR")){auto result=std::filesystem::path(overridePath);std::filesystem::create_directories(result);return result;}
    @autoreleasepool{
        NSURL* path=[[[NSFileManager defaultManager]URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask]firstObject];
        auto result=std::filesystem::path(path.fileSystemRepresentation)/"FrameRender";std::filesystem::create_directories(result);return result;
    }
}
bool confirmDiscard(){
    @autoreleasepool{
        NSAlert* alert=[[NSAlert alloc]init];alert.messageText=@"Discard unsaved changes?";alert.informativeText=@"Save your project before continuing if you want to keep these changes.";
        [alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Discard changes"];
        return [alert runModal]==NSAlertSecondButtonReturn;
    }
}
void reveal(const std::filesystem::path& path){@autoreleasepool{[[NSWorkspace sharedWorkspace]activateFileViewerSelectingURLs:@[[NSURL fileURLWithFileSystemRepresentation:path.c_str() isDirectory:NO relativeToURL:nil]]];}}
}
