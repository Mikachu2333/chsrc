/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * -------------------------------------------------------------
 * File Name     : struct.h
 * File Authors  : @ccmywish
 *               | @G_I_Y
 * Contributors  : @livelycode36
 *               |
 * Created On    : <2023-08-29>
 * Last Modified : <2026-08-14>
 *
 * chsrc struct
 * ------------------------------------------------------------*/

typedef struct ProviderSpeedMeasureInfo_t
{
  bool  skip;           /* 是否默认跳过 */
  char *skip_reason_ZH; /* 跳过的原因（中文）*/
  char *skip_reason_EN; /* 跳过的原因（英文）*/
  char *url;            /* 测速链接 */
  bool  accurate;       /* 是否为精准测速，上游源和专用镜像站为 ACCURATE，通用镜像站为 ROUGH */
}
ProviderSpeedMeasureInfo_t;

#define SKIP    true
#define NotSkip false
#define ToFill  NULL
#define NA		  NULL

#define ACCURATE true
#define ROUGH    false

typedef enum ProviderType_t
{
  IS_GeneralMirrorSite,   /* 通用镜像站 */
  IS_DedicatedMirrorSite, /* 专用镜像站 */
  IS_UpstreamProvider,    /* 上游默认源 */
  IS_UserDefinedProvider, /* 用户提供   */
}
ProviderType_t;

typedef struct SourceProvider_t
{
  const ProviderType_t type; /* 类型 */
  const char *code; /* 用于用户指定某一 Provider */
  const char *abbr; /* 需要使用 Provider 的英文名时，用这个代替，因为大部分 Provider 没有提供正式的英文名 */
  const char *name; /* Provider 中文名 */
  const char *site; /* Provider 首页   */
  ProviderSpeedMeasureInfo_t psmi;
}
SourceProvider_t;

typedef SourceProvider_t MirrorSite_t;

SourceProvider_t UpstreamProvider =
{
  IS_UpstreamProvider,
  /* 引入新的上游默认源时，请使下面第一行的前三个字段保持不变，只添加第四个字段 */
  "upstream", "Upstream", "上游默认源", NULL,
  /* 引入新的上游默认源时，请完全修改下面这个结构体，可使用 def_need_measure_info 宏 */
  {SKIP, "URL未知，邀您参与贡献!", "URL unknown, welcome to contribute!", NULL, ACCURATE}
};

#define def_need_measure_info   {SKIP, "缺乏较大的测速对象，邀您参与贡献!", "Lack of large object URL, welcome to contribute!", NULL, ACCURATE}

SourceProvider_t UserDefinedProvider =
{
  IS_UserDefinedProvider,
  "user", "用户自定义", "用户自定义", NULL,
  {SKIP, "用户自定义源不测速", "SKIP for user-defined source", NULL, ACCURATE}
};


typedef struct Source_t
{
  union {
    SourceProvider_t *provider;
    MirrorSite_t     *mirror;
  };
  /* 用于换源的 URL，也称 repoURL */
  char *url;

  /* 对该 source 的专用测速链接，这就是精准测速，也称 smURL */
  char *speed_measure_url;
}
Source_t;

/* 不用给专用测速链接，因为 Upstream 的整体测速链接已是精准测速 */
#define DelegateToUpstream  NULL
/* 不用给专用测速链接，因为该镜像站是专用镜像站，其整体测速链接已是精准测速 */
#define DelegateToMirror    NULL
/* 看到该注释的贡献者，你可以帮忙寻找专用测速链接 */
#define NeedContribute      NULL
/* 由 _prepare() 填充 */
#define FeedByPrepare NULL


/**
 * 换源的作用域
 *
 * 在 chsrc v0.2.4.2 以前，我们一直使用的是 `-local` 这个选项，其含义是启用 *项目级* 换源
 *
 * 1. 默认不使用该选项时，含义是 *全局* 换源，
 *
 *    全局分为 (1)系统级 (2)用户级
 *
 *    大多数第三方配置软件往往默认进行的是 *用户级* 的配置。所以 chsrc 首先将尝试使用 *用户级* 配置
 *
 * 2. 若不存在 *用户级* 的配置，chsrc 将采用 *系统级* 的配置
 *
 * 3. 最终效果本质由第三方软件决定，如 poetry 默认实现的就是项目级的换源
 *
 * 但是后来，我们认为非 -local 时的行为（即默认时）比较模糊，所以我们现在清晰地把作用域指明出来，总共有3种类型的作用：
 * 分别是 ProjectScope、UserScope 和 SystemScope，分别对应项目级、用户级和系统级的换源配置
 *
 * 还有一个叫 ImplementationDefinedScope 的作用域，它不是一种新类型，而是表示根据实际情况决定的作用域。
 * chsrc 将根据该 target 的实际情况来选择最合适的作用域来进行换源配置。最好的情况下，ImplementationDefinedScope 是三者之一，
 * 这也是这里设计的初衷。然而现在有些 recipe 的换源行为，会在某种 Scope 不能够成功时退而求其次地使用另一个 Scope
 * 来进行换源配置，这时就只能用 ImplementationDefinedScope 来表示。
 */
#define NumberOfScopeType    3
 typedef enum Scope_t
{
  ProjectScope,
  UserScope,
  SystemScope,

  /**
   * 这是 target 默认的作用域，一种特殊的作用域，即根据 target 的实际情况来决定的。
   * 它不是一种真正的类型，因为最终换源后，用户看到的作用域依然是 ProjectScope、UserScope 或 SystemScope 中的一个
   */
  ImplementationDefinedScope,
}
Scope_t;

#define ScopeCap_Slot_Project 0
#define ScopeCap_Slot_User    1
#define ScopeCap_Slot_System  2

typedef enum ScopeCapability_t
{
  ScopeCap_Unknown,                  /* 未知，缺乏对该 target 的细致了解 */
  ScopeCap_Unable,                   /* 不支持该作用域 */
  ScopeCap_Able_But_Not_Implemented, /* 支持但chsrc尚未实现 */
  ScopeCap_Able_And_Implemented      /* 支持且chsrc已经实现 */
}
ScopeCapability_t;

typedef enum Capability_t
{
  CanNot,
  FullyCan,
  PartiallyCan
}
Capability_t;



typedef struct Contributor_t
{
  char *id;     /* 全局唯一贡献者标识符，防止反复写信息，以 @ 开头 */
  char *name;   /* 贡献者姓名; 鉴于该项目完全依赖于贡献者，建议留下真实姓名或者昵称 */
  char *email;
  char *display_name; /* recipe 结束时会显示贡献者信息，如果你不愿显示真实姓名或者昵称，可以另外提供一个名字 */
}
Contributor_t;


typedef struct Target_t
{
  /* 以 / 为分隔符的多个目标别名 */
  char *aliases;

  void (*getfn)   (char *option);
  void (*setfn)   (char *option);
  void (*resetfn) (char *option);

  /**
   * 初始化函数，用于填充该 struct 的各种信息
   *
   * 值得注意的是，preparefn() 将初始化该结构体内三个最重要的函数:
   * 即 getfn setfn resetfn 的函数地址，但是 preparefn 这个函数自
   * 身的函数地址不可能由自己初始化，所以需要额外的位置初始化:
   *
   *   1. menu.c 中通过 add() 注册
   *   2. recipe 内部手动调用，如 "源target"
   */
  void (*preparefn) (void);
  bool inited; /* 是否执行过了 preparefn() */

  XySeq_t *sub_dishes; /* 某些 target 为 group target，几乎完全由 sub dishes 定义 */

  Source_t *sources;
       int  sources_n;


  /* Features */
  bool  can_english;        /* 是否支持英文输出 */

  bool  can_user_define;    /* 是否支持用户自定义URL来换源 */
  char *can_user_define_explain; /* 用户自定义URL的说明 */

  /**
   * 各作用域的支持情况
   * 参考 ScopeCap_Slot_Xxx 的值
   */
  ScopeCapability_t scope_caps[NumberOfScopeType];
  Scope_t default_scope;   /* 默认作用域 */

  char *note;              /* 备注 */


  /* recipe 维护信息 */
  char *created_on;
  char *last_updated;

  XySeq_t *chefs;    /* 该 recipe 的主要作者   */
  XySeq_t *sauciers; /* 该 recipe 的次要贡献者 */
}
Target_t;



#define def_target(t, aliases) void t##_getsrc(char *option);void t##_setsrc(char *option);void t##_resetsrc(char *option); Target_t t##_target={aliases};

/* 仅内部使用的 "源target"，只用来存储源信息，请参考 pl_nodejs_binary 以及 pl_pypi */
#define def_sources_target(t, name) Target_t t##_target={"__internal_target_only_for_storing_sources__" name "__"}

/* group target 不需要实现任何操作(除了preparefn)即可使用 */
#define def_group_target(t, aliases) Target_t t##_target={aliases};

#define chef_allow_gsr(t) this->getfn = t##_getsrc; this->setfn = t##_setsrc; this->resetfn = t##_resetsrc;
#define chef_allow_s(t)   this->getfn = NULL;       this->setfn = t##_setsrc; this->resetfn = NULL;
#define chef_allow_sr(t)  this->getfn = NULL;       this->setfn = t##_setsrc; this->resetfn = t##_resetsrc;
#define chef_allow_gs(t)  this->getfn = t##_getsrc; this->setfn = t##_setsrc; this->resetfn = NULL;
#define chef_allow_NOOP(t)
#define chef_prep_this(t,op) Target_t *this = &t##_target; this->inited = true; chef_allow_##op(t);

/* 简化 "源target" 的编写 */
#define chef_prep_sources_target(t) Target_t *this = &t##_target; this->inited = true; chef_allow_NOOP(t);

/* 内部 "源target" 的 prepare 未通过 menu.c 的 add() 注册, 手动挂载 */
#define chef_set_preparefn_for_sources_target(t) t##_target.preparefn = t##_prepare;


#define chef_use_this(t) Target_t *this = &t##_target;
#define chsrc_use_this_source(t) Target_t *this = &t##_target; Source_t source = chsrc_yield_source_and_confirm (this, option);

/**
 * 用于定义换源列表
 *
 *   def_sources_begin()
 *   {&UpstreamProvider, "换源URL", "精准测速链接"},
 *   {&某镜像站1,        "换源URL", "精准测速链接"},
 *   {&某镜像站2,        "换源URL",  NULL },  // 若精准测速链接为空，则为模糊测速，默认使用该镜像站的整体测速链接
 *   def_sources_end()
 *
 *   出于代码美观考虑，上述第三列可以写 FeedByPrepare，然后下面使用文档 ./doc/11-如何设置换源链接与测速链接.md 中的函数来填充
 */
#define def_sources_begin()  Source_t sources[] = {
#define def_sources_end()    }; \
  this->sources_n = xy_c_array_len(sources); \
  char *_sources_storage = xy_malloc0 (sizeof(sources)); \
  memcpy (_sources_storage, sources, sizeof(sources)); \
  this->sources = (Source_t *)_sources_storage;
