import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ReleaseAudioSafetyTest(unittest.TestCase):
    def test_compiled_protocol_and_playback_invariants(self):
        compiler = shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++ host compiler is required')
        source = r'''
#include "main/audio/playback_fence.h"
#include "main/protocols/audio_wire_validation.h"
#include "main/release_manifest.h"
#include <cassert>
#include <thread>
int main() {
 PlaybackFence f; auto old=f.epoch();
 assert(!f.Start(old,"old",1)); f.ExpectResponse(); assert(f.Start(old,"old",1));
 assert(f.Current(old,"old",1)); assert(!f.Stop(old,"other",1));
 f.Invalidate(); assert(!f.Current(old)); assert(!f.Start(old,"late",2));
 f.ExpectResponse(); auto fresh=f.epoch(); assert(!f.Start(fresh,"old",1));
 assert(f.Start(fresh,"new",2)); assert(!f.Current(fresh,"old",1));
 assert(!f.Stop(fresh,"new",1)); assert(f.Stop(fresh,"new",2));
 assert(!f.Start(fresh,"new",2));
 f.ExpectResponse(); assert(!f.Start(fresh,"replayed",1));
 assert(f.Start(fresh,"third",3));
 std::thread abort([&]{f.Invalidate();}); abort.join(); assert(!f.Current(fresh));
 unsigned char packet[17]{}; packet[1]=2; packet[15]=1;
 audio_wire::View out{};
 for(size_t n=0;n<17;++n) assert(!audio_wire::Parse(packet,n,2,out));
 assert(audio_wire::Parse(packet,17,2,out)); assert(out.size==1);
 packet[15]=2; assert(!audio_wire::Parse(packet,17,2,out));
 unsigned char v3[5]={0,0,0,1,1}; assert(audio_wire::Parse(v3,5,3,out));
 v3[0]=1; assert(!audio_wire::Parse(v3,5,3,out));
 unsigned char raw[8193]{}; assert(!audio_wire::Parse(raw,8193,1,out));
 assert(audio_wire::Parse(raw,8192,1,out)); assert(!audio_wire::Parse(raw,1,99,out));
 assert(audio_wire::Parameters(16000,60)); assert(audio_wire::Parameters(24000,120)); assert(!audio_wire::Parameters(0,60));
 assert(!audio_wire::Parameters(24000,9999));
 ReleaseManifest m; m.valid=true; m.board_type="board";m.board_name="release";m.chip="esp32s3";
 m.min_flash_size=16<<20;m.size=4096;m.assets_size=8192;m.sha256=std::string(64,'a');
 m.assets_sha256=std::string(64,'b');m.url="https://example.test/fw?ticket=opaque";m.update_policy="automatic";
 assert(release_manifest::Compatible(m,"board","release","esp32s3",16<<20));
 assert(!release_manifest::Compatible(m,"other","release","esp32s3",16<<20));
 assert(!release_manifest::Compatible(m,"board","dev","esp32s3",16<<20));
 assert(!release_manifest::Compatible(m,"board","release","esp32c3",16<<20));
 assert(!release_manifest::Compatible(m,"board","release","esp32s3",8<<20));
 m.assets_required=true;assert(!release_manifest::Compatible(m,"board","release","esp32s3",16<<20));
 assert(!release_manifest::Https("http://example.test/fw"));
 assert(!release_manifest::Https("https://user@example.test/fw"));
 assert(!release_manifest::Https("https://example.test/fw\r\nX: y"));
 assert(!release_manifest::Sha256(std::string(64,'G')));
 assert(release_manifest::AllowedUrl("http://192.168.3.8:8002/ota/",true));
 assert(release_manifest::AllowedUrl("http://127.0.0.1:8002/ota/",true));
 assert(release_manifest::AllowedUrl("http://172.16.0.1/ota",true));
 assert(!release_manifest::AllowedUrl("http://172.32.0.1/ota",true));
 assert(!release_manifest::AllowedUrl("http://8.8.8.8/ota",true));
 assert(!release_manifest::AllowedUrl("http://192.168.1.1.evil.test/ota",true));
 assert(!release_manifest::AllowedUrl("http://192.168.1.1/ota",false));
 assert(!release_manifest::AllowedUrl("http://192.168.1.1@evil.test/ota",true));
 assert(release_manifest::SameOrigin("http://192.168.1.1:8002/ota/","http://192.168.1.1:8002/download?ticket=x"));
 assert(!release_manifest::SameOrigin("https://trusted.test/ota/","https://evil.test/download"));
 assert(release_manifest::SameArtifactUrl("https://trusted.test/release/a/app?ticket=old","https://trusted.test/release/a/app?ticket=new"));
 assert(!release_manifest::SameArtifactUrl("https://trusted.test/release/a/app?ticket=old","https://trusted.test/release/b/app?ticket=new"));
 assert(!release_manifest::SameArtifactUrl("https://trusted.test/release/a/app","https://evil.test/release/a/app"));
 assert(!release_manifest::SameArtifactUrl("https://trusted.test/release/a/app","https://trusted.test/release/a/assets"));
 assert(!release_manifest::SameArtifactUrl("http://192.168.3.152/a","http://192.168.3.152/a"));
 assert(release_manifest::SameArtifactUrl("http://192.168.3.152/a?ticket=old","http://192.168.3.152/a?ticket=new",true));
 f.NewConnection();f.ExpectResponse();assert(f.Start(f.epoch(),"new-session",0));
 assert(release_manifest::TrustedOtaRoot("http://192.168.3.8:19183/xiaozhi/ota/","https://old-public-cloud.test/ota/",true,true)=="http://192.168.3.8:19183/xiaozhi/ota/");
 assert(release_manifest::TrustedOtaRoot("","https://old-public-cloud.test/ota/",true,true).empty());
 assert(release_manifest::TrustedOtaRoot("http://public.test/ota/","https://old-public-cloud.test/ota/",true,true).empty());
 assert(release_manifest::TrustedOtaRoot("http://192.168.3.8:19183/ota/","https://old-public-cloud.test/ota/",true,false).empty());
 assert(release_manifest::TrustedOtaRoot("https://new.test/ota/","https://old-public-cloud.test/ota/",false,false)=="https://old-public-cloud.test/ota/");
 assert(release_manifest::TrustedOtaRoot("https://new.test/ota/","",false,false)=="https://new.test/ota/");
}
'''
        with tempfile.TemporaryDirectory(prefix='xiaozhi-release-host-') as tmp:
            cpp=pathlib.Path(tmp)/'safety.cc';binary=pathlib.Path(tmp)/'safety'
            cpp.write_text(source)
            subprocess.run([compiler,'-std=c++17','-pthread','-I',str(ROOT),str(cpp),'-o',str(binary)],check=True,capture_output=True)
            subprocess.run([str(binary)],check=True,capture_output=True)


if __name__ == '__main__':
    unittest.main()
