#pragma once
#include <array>

namespace voice_controls {
struct Command {
    const char* pinyin;
    const char* text;
    const char* action;
};
// App-side grammar additions: existing resource/font/model bytes stay compatible.
inline constexpr std::array<Command, 26> kCommands{{
    {"bie shuo le", "别说了", "stop"},
    {"ting zhi bo fang", "停止播放", "stop"},
    {"tui chu", "退出", "exit"},
    {"zai jian", "再见", "exit"},
    {"tui xia ba", "退下吧", "exit"},
    {"jie shu dui hua", "结束对话", "exit"},
    {"ting zhi", "停止", "stop"},
    {"ting zhi bo bao", "停止播报", "stop"},
    {"ting zhi shuo hua", "停止说话", "stop"},
    {"ting zhi hui da", "停止回答", "stop"},
    {"bie bie shuo le", "别别说了", "stop"},
    {"bu yao shuo le", "不要说了", "stop"},
    {"bu yong shuo le", "不用说了", "stop"},
    {"ting yi xia", "停一下", "stop"},
    {"zan ting bo fang", "暂停播放", "stop"},
    {"zan ting yi xia", "暂停一下", "stop"},
    {"da duan yi xia", "打断一下", "stop"},
    {"zan ting", "暂停", "stop"},
    {"zan ting bo bao", "暂停播报", "stop"},
    {"xian ting yi xia", "先停一下", "stop"},
    {"xian bie shuo le", "先别说了", "stop"},
    {"tui xia", "退下", "exit"},
    {"tui xia le", "退下了", "exit"},
    {"tui yi xia ba", "退一下吧", "exit"},
    {"zan ting tui xia", "暂停退下", "exit"},
    {"ting zhi zan ting tui xia", "停止暂停退下", "exit"},
}};
}  // namespace voice_controls
