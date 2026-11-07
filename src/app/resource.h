#pragma once

// 资源 ID。app.rc 里的图标和版本信息都用这里的定义。
//
// 只定义**一个**图标 ID：同一个 .ico 里带了 16~256 共 10 个尺寸，
// LoadIcon 会按系统要的尺寸自己挑，所以大图标和小图标共用一份资源。
// 分成两组（IDI_APP_ICON / IDI_APP_ICON_SMALL）会让 exe 里出现两个
// RT_GROUP_ICON 组，而资源管理器只认 ID 最小的那组 —— 白白多一倍体积。
#define IDI_APP_ICON 101
