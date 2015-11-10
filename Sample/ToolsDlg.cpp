#include "stdafx.h"
#include "Sample.h"
#include "ToolsDlg.h"
#include "PresetDlg.h"
#include "SessionDlg.h"
#include "LogDlg.h"
#include "LogMgr.h"
#include <algorithm>
#include <io.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// 格式化码率，返回易读性的码率字符串
static void inline FriendlyBitrate(CString& fmt, int bitrate) {
    if (bitrate > 1000 * 1000) {
        fmt.Format("%.2f mbps", bitrate / 1000000.0f);
    } else if (bitrate > 1000) {
        fmt.Format("%d kbps", bitrate / 1000);
    } else  {
        fmt.Format("%d bps", bitrate);
    }
}

// 视频信息排序比较函数对象
struct VideoInfoPred {
    int operator()(const lc_video_info_t& i1, const lc_video_info_t& i2) {
        return !less_than(i1, i2);
    }
    int less_than(const lc_video_info_t& i1, const lc_video_info_t& i2) {
        if (i1.width_in_pixel < i2.width_in_pixel) {
            return 1;
        } else if (i1.width_in_pixel > i2.width_in_pixel) {
            return 0;
        }

        if (i1.height_in_pixel < i2.height_in_pixel) {
            return 1;
        } else if (i1.height_in_pixel > i2.height_in_pixel) {
            return 0;
        }

        if (i1.avg_time_per_frame_in_100ns > i2.avg_time_per_frame_in_100ns) {
            return 1;
        } else if (i1.avg_time_per_frame_in_100ns < i2.avg_time_per_frame_in_100ns) {
            return 0;
        }

        if (i1.pixel_depth_in_bit < i2.pixel_depth_in_bit) {
            return 1;
        } else if (i1.pixel_depth_in_bit > i2.pixel_depth_in_bit) {
            return 0;
        }

        if (i1.interlaced > i1.interlaced) {
            return 1;
        } else if (i1.interlaced < i2.interlaced) {
            return 0;
        }

        if (i1.format_fourcc < i2.format_fourcc) {
            return 1;
        }

        return 0;
    }
};

CString video_info_string(const lc_video_info_t& info) {
    CString strFmt;
    char fourcc[5] = { 0 };

    if (info.format_fourcc <= BI_BITFIELDS) {
        strncpy_s(fourcc, "RGB", -1);
    } else {
        strncpy_s(fourcc, (const char*)(&info.format_fourcc), 4);
    }

    strFmt.Format("%dx%d %.2ffps %s %dbit %s",
                  info.width_in_pixel, info.height_in_pixel,
                  info.avg_time_per_frame_in_100ns ?
                  10000000.0f / info.avg_time_per_frame_in_100ns :
                  0.0f,
                  fourcc, info.pixel_depth_in_bit,
                  info.interlaced ? "i" : "p");

    return strFmt;
}

// 设置日志回调函数，将日志转入日志管理类
void LC_API lc_log_callback(LC_LOGLEVEL level, const char* msg) {
    CLogMgr::Instance().AppendLog(level, msg);
}


#define WM_USER_STATUS_CHANGED (WM_APP +1)

CToolsDlg::CToolsDlg(CWnd* pParent /*=NULL*/)
    : CDialog(CToolsDlg::IDD, pParent)
    , m_handle(0)
    , m_presets(0)
    , m_pLogDlg(NULL)
    , m_pPresetDlg(NULL)
    , m_pSessionDlg(NULL)
    , m_strConfigFile("")
    , m_uTimer(0) {
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}
CToolsDlg::~CToolsDlg() {
    if (m_handle) {
        lc_close(m_handle);
        m_handle = 0;
    }

    if (m_presets) {
        lc_preset_list_free(m_presets);
        m_presets = 0;
    }
}
void CToolsDlg::DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_BUTTON_AUTO_PRESET, m_BtnDetectPreset);
    DDX_Control(pDX, IDC_BUTTON_SELECT_PATH, m_BtnSelectPath);
    DDX_Control(pDX, IDC_BUTTON_CLOSE, m_BtnClose);
    DDX_Control(pDX, IDC_BUTTON_STOP, m_BtnStop);
    DDX_Control(pDX, IDC_BUTTON_START, m_BtnStart);
    DDX_Control(pDX, IDC_COMBO_VIDEO, m_CmbVideoDevices);
    DDX_CBString(pDX, IDC_COMBO_VIDEO, m_Model.m_VideoDevice);
    DDX_Control(pDX, IDC_COMBO_AUDIO, m_CmbAudioDevices);
    DDX_CBString(pDX, IDC_COMBO_AUDIO, m_Model.m_AudioDevice);
    DDX_Control(pDX, IDC_COMBO_PRESET, m_CmbPresets);
    DDX_CBString(pDX, IDC_COMBO_PRESET, m_Model.m_Preset);
    DDX_Control(pDX, IDC_CHECK_SAVE_LOCAL, m_ChkSaveLocal);
    DDX_Check(pDX, IDC_CHECK_SAVE_LOCAL, m_Model.m_SaveLocal);
    DDX_Control(pDX, IDC_EDIT_LOCAL_PATH, m_EdtLocalPath);
    DDX_Text(pDX, IDC_EDIT_LOCAL_PATH, m_Model.m_LocalSavePath);
    DDX_Control(pDX, IDC_EDIT_BOSBUCKET, m_EdtBosBucket);
    DDX_Text(pDX, IDC_EDIT_BOSBUCKET, m_Model.m_BCEBucket);
    DDX_Control(pDX, IDC_EDIT_USERDOMAIN, m_EdtUserDomain);
    DDX_Text(pDX, IDC_EDIT_USERDOMAIN, m_Model.m_BCEUserDomain);
    DDX_Control(pDX, IDC_EDIT_SESSION_NAME, m_EdtSessionName);
    DDX_Text(pDX, IDC_EDIT_SESSION_NAME, m_Model.m_BCESessionName);
    DDX_Control(pDX, IDC_EDIT_RTMP, m_EdtUserRtmp);
    DDX_Radio(pDX, IDC_RADIO_EXISTING_SESSION, m_Model.m_RtmpOption);
    DDX_Text(pDX, IDC_EDIT_RTMP, m_Model.m_UserRTMPUrl);
    DDX_Control(pDX, IDC_EDIT_HLS_URL, m_EdtHLSPlayUrl);
    DDX_Text(pDX, IDC_EDIT_HLS_URL, m_Model.m_HLSPlayUrl);
    DDX_Control(pDX, IDC_EDIT_RTMP_URL, m_EdtRtmpPlayUrl);
    DDX_Text(pDX, IDC_EDIT_RTMP_URL, m_Model.m_RTMPPlayUrl);
    DDX_Control(pDX, IDC_STATIC_STATUS, m_staStatus);
    DDX_Text(pDX, IDC_STATIC_STATUS, m_Model.m_Status);
    DDX_Control(pDX, IDC_EDIT_ERROR_DETAIL, m_edtErrorDetail);
    DDX_Control(pDX, IDC_RADIO_EXISTING_SESSION, m_rdoExistRtmp);
    DDX_Control(pDX, IDC_RADIO_RTMP, m_rdoUserRtmp);
    DDX_Control(pDX, IDC_RADIO_BCE, m_rdoBceRtmp);
    DDX_Control(pDX, IDC_BUTTON_REFRESH_DATA, m_btnRefresh);
    DDX_Control(pDX, IDC_EDIT_SESSION, m_edtExistingSession);
    DDX_Control(pDX, IDC_BUTTON_SELECT_SESSION, m_btnSelectSession);
    DDX_Text(pDX, IDC_EDIT_SESSION, m_Model.m_BCESessionId);
    DDX_Control(pDX, IDC_COMBO_VIDEO_INFO, m_cmbVideoInfos);
    DDX_Control(pDX, IDC_EDIT_STATIS, m_edtStatis);
}

BEGIN_MESSAGE_MAP(CToolsDlg, CDialog)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(IDC_BUTTON_AUTO_PRESET, &CToolsDlg::OnBnClickedButtonAutoPreset)
    ON_BN_CLICKED(IDC_BUTTON_SELECT_PATH, &CToolsDlg::OnBnClickedButtonSelectPath)
    ON_BN_CLICKED(IDC_BUTTON_CLOSE, &CToolsDlg::OnBnClickedButtonClose)
    ON_BN_CLICKED(IDC_BUTTON_STOP, &CToolsDlg::OnBnClickedButtonStop)
    ON_BN_CLICKED(IDC_BUTTON_START, &CToolsDlg::OnBnClickedButtonStart)
    ON_BN_CLICKED(IDC_RADIO_RTMP, &CToolsDlg::OnBnClickedRadioRtmp)
    ON_BN_CLICKED(IDC_RADIO_BCE, &CToolsDlg::OnBnClickedRadioBce)
    ON_BN_CLICKED(IDC_CHECK_SAVE_LOCAL, &CToolsDlg::OnBnClickedCheckSaveLocal)
    ON_BN_CLICKED(IDC_BUTTON_REFRESH_DATA, &CToolsDlg::OnBnClickedButtonRefreshData)
    ON_MESSAGE(WM_USER_STATUS_CHANGED, OnStatusChanged_WM)
    ON_WM_TIMER()
    ON_COMMAND(IDM_PRESET, &CToolsDlg::OnMenuPreset)
    ON_UPDATE_COMMAND_UI(IDM_PRESET, &CToolsDlg::OnUpdateMenuPreset)
    ON_COMMAND(IDM_SESSION, &CToolsDlg::OnMenuSession)
    ON_UPDATE_COMMAND_UI(IDM_SESSION, &CToolsDlg::OnUpdateMenuSession)
    ON_COMMAND(IDM_LOG, &CToolsDlg::OnMenuLog)
    ON_UPDATE_COMMAND_UI(IDM_LOG, &CToolsDlg::OnUpdateMenuLog)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_BUTTON_SELECT_SESSION, &CToolsDlg::OnBnClickedButtonSelectSession)
    ON_BN_CLICKED(IDC_RADIO_EXISTING_SESSION, &CToolsDlg::OnBnClickedRadioExistingSession)
    ON_CBN_SELCHANGE(IDC_COMBO_VIDEO_INFO, &CToolsDlg::OnCbnSelchangeComboVideoInfo)
    ON_CBN_SELCHANGE(IDC_COMBO_VIDEO, &CToolsDlg::OnCbnSelchangeComboVideo)
    ON_WM_CREATE()
END_MESSAGE_MAP()


BOOL CToolsDlg::OnInitDialog() {
    m_Model.Deserailize();

    CDialog::OnInitDialog();

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // 读取配置文件
    CString& config = GetConfigFile();


    if (!config.IsEmpty()) {
        // 如果配置文件不存在，创建默认的配置文件
        if (0 != _access(config, 0)) {
            CLogMgr::Instance().AppendLog(LC_LOG_ERROR, "配置文件不存存在，创建默认配置文件");

            static char s_default_config[] =
                "[system]\r\n"
                "access_key_id=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n"
                "secret_access_key=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\r\n"
                "host=http://lss.bj.baidubce.com\r\n";

            CFile file;
            TRY

            if (!file.Open(config, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite)) {
                CLogMgr::Instance().AppendLog(LC_LOG_ERROR, "创建默认配置文件失败");
            } else {
                file.Write(s_default_config, sizeof(s_default_config));
                file.Close();
            }

            CATCH_ALL(e)
            DELETE_EXCEPTION(e);
            CLogMgr::Instance().AppendLog(LC_LOG_ERROR, "创建默认配置文件异常");
            END_CATCH_ALL
        }

        CString ak;
        CString sk;
        CString host;

        // 读取 ak/sk, 以及服务器地址
        GetPrivateProfileString("system", "access_key_id", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                ak.GetBuffer(33), 33, config);
        ak.ReleaseBuffer();

        GetPrivateProfileString("system", "secret_access_key", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                                sk.GetBuffer(33), 33, config);
        sk.ReleaseBuffer();

        GetPrivateProfileString("system", "host", "http://lss.bj.baidubce.com",
                                host.GetBuffer(130), 130, config);
        host.ReleaseBuffer();

        lc_bce_access_key_t key = { 0 };
        strncpy_s(key.access_key_id, (LPCSTR)ak, -1);
        strncpy_s(key.secret_access_key, (LPCSTR)sk, -1);

        // 初始化采集SDK
        if (LC_OK != lc_init(&key, host)) {
            CLogMgr::Instance().AppendLog(LC_LOG_ERROR, "lc_init failed.");
            MessageBox("初始化采集模块失败");
        }

        // 设置日志回调
        lc_log_set_callback(lc_log_callback);
    } else {
        MessageBox("加载配置文件失败，无法定位程序目录");
    }

    //更新视频、音频设备，更新录制模板列表
    UpdateLiveCaptureData();

    // 更新界面状态
    OnBnClickedRadioRtmp();
    OnBnClickedCheckSaveLocal();
    UpdateStatus();

    EnableToolTips();

    return TRUE;
}

// 根据程序目录，生成配置文件路径
CString& CToolsDlg::GetConfigFile() {
    if (m_strConfigFile.IsEmpty()) {
        char* pgm = NULL;
        _get_pgmptr(&pgm);

        if (!pgm) {
            return m_strConfigFile;
        }

        CString strPath(pgm);
        int n = max(strPath.ReverseFind('/'), strPath.ReverseFind('\\'));

        if (n < 0) {
            return m_strConfigFile;
        }

        strPath.SetAt(n + 1, 0);
        strPath.ReleaseBuffer();

        strPath.Append("config.ini");
        m_strConfigFile = strPath;
    }

    return m_strConfigFile;
}

// 更新视频设备，音频设备，和录制模板列表
void CToolsDlg::UpdateLiveCaptureData() {
    CString oldAudioName;
    int oldAudioIndex = m_CmbAudioDevices.GetCurSel();

    // 保存旧的音频设备名称
    if (oldAudioIndex != CB_ERR) {
        m_CmbAudioDevices.GetLBText(oldAudioIndex, oldAudioName);
    }

    // 刷新音频设备ComboBox
    m_CmbAudioDevices.ResetContent();
    lc_audio_device_t audioDevice = { 0 };
    // 调用API, 得到音频设备数量
    int audios = lc_audio_device_get_count();

    for (int i = 0; i < audios; i ++) {
        // 调用API, 得到第i个音频设备信息
        if (LC_OK == lc_audio_device_get_device(i, &audioDevice)) {
            m_CmbAudioDevices.AddString(audioDevice.device_name);
        }
    }

    // 根据旧的音频设备，选择新的音频设备
    if (oldAudioName.IsEmpty() ||
            m_CmbAudioDevices.SelectString(-1, oldAudioName) < 0) {
        m_CmbAudioDevices.SetCurSel(0);
    }


    // 保存旧的视频设备名称
    CString oldVideoName;
    int oldVideoIndex = m_CmbVideoDevices.GetCurSel();

    if (oldVideoIndex != CB_ERR) {
        m_CmbVideoDevices.GetLBText(oldVideoIndex, oldVideoName);
    }

    // 刷新视频设备ComboBox
    m_CmbVideoDevices.ResetContent();
    lc_video_device_t videoDevice = { 0 };
    // 调用API, 得到视频设备数量
    int videos = lc_video_device_get_count();

    for (int i = 0; i < videos; i ++) {
        // 调用API, 得到第i个视频设备信息
        if (LC_OK == lc_video_device_get_device(i, &videoDevice)) {
            m_CmbVideoDevices.AddString(videoDevice.device_name);
        }
    }

    // 根据旧的视频设备，选择新的视频设备
    if (oldVideoName.IsEmpty() ||
            m_CmbVideoDevices.SelectString(-1, oldVideoName) < 0) {
        m_CmbVideoDevices.SetCurSel(0);
    }

    // 更新视频设备支持的视频格式
    UpdateVideoInfos();

    //保存旧的模板名称
    CString oldPresetName;
    int oldPresetIndex = m_CmbPresets.GetCurSel();

    if (oldPresetIndex != CB_ERR) {
        m_CmbPresets.GetLBText(oldPresetIndex, oldPresetName);
    }

    // 刷新模板ComboBox
    m_CmbPresets.ResetContent();

    if (m_presets) {
        // 调用API, 释放原有的模板列表
        lc_preset_list_free(m_presets);
        m_presets = NULL;
    }

    // 调用API, 从服务端读取模板列表
    if (LC_OK == lc_preset_list(&m_presets)) {
        for (int i = 0 ; i < m_presets->count; i++) {
            int iIndex =  m_CmbPresets.AddString(m_presets->preset_list[i].presetName);

            if (iIndex >= 0) {
                m_CmbPresets.SetItemData(iIndex, (DWORD_PTR)&m_presets->preset_list[i]);
            }
        }
    } else {
        CString strMsg;
        strMsg.Format("读取直播模板失败\r\n%s", lc_get_last_error());
        MessageBox(strMsg);
    }

    // 根据旧的模板名称，选择新的模板
    if (oldPresetName.IsEmpty() ||
            m_CmbPresets.SelectString(-1, oldPresetName) < 0) {
        m_CmbPresets.SetCurSel(0);
    }
}

void CToolsDlg::OnPaint() {
    if (IsIconic()) {
        CPaintDC dc(this); // device context for painting

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // Center icon in client rectangle
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // Draw the icon
        dc.DrawIcon(x, y, m_hIcon);
    } else {
        CDialog::OnPaint();
    }
}

HCURSOR CToolsDlg::OnQueryDragIcon() {
    return static_cast<HCURSOR>(m_hIcon);
}


void CToolsDlg::OnBnClickedButtonAutoPreset() {

}

// 选择录制文件存储路径
void CToolsDlg::OnBnClickedButtonSelectPath() {
    CFileDialog cfd(FALSE, "flv", "live", OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
                    "*.flv|*.flv|All|*||", this);

    if (IDOK == cfd.DoModal()) {
        m_EdtLocalPath.SetWindowText(cfd.GetPathName());
    }
}

//关闭程序
void CToolsDlg::OnBnClickedButtonClose() {
    EndDialog(0);
}

// 停止录制
void CToolsDlg::OnBnClickedButtonStop() {
    if (m_handle) {
        lc_stop(m_handle);
        lc_close(m_handle);
        m_handle = NULL;
        OnStop();
    }

    if (m_uTimer) {
        KillTimer(m_uTimer);
        m_uTimer = 0;
    }

    UpdateStatus();
}

// 更新录制状态
void CToolsDlg::UpdateStatus() {
    LC_STATUS status = LC_STATUS_READY;

    if (m_handle) {
        // 调用API, 得到采集对象状态
        status = lc_get_status(m_handle);
    }

    UpdateStatusUi(status, LC_OK);
}

void CToolsDlg::UpdateStatusUi(LC_STATUS status, LC_CODE code) {

    CString strStatusName;
    int running = 0;

    switch (status) {
    case LC_STATUS_STARTING:
        strStatusName = "启动中...";
        running = 1;
        break;

    case LC_STATUS_RUNNING:
        strStatusName = "运行中...";
        running = 1;
        break;

    case LC_STATUS_RETRYING:
        strStatusName = "重试中...";
        running = 1;
        break;

    case LC_STATUS_READY:
        strStatusName = "就绪";
        running = 0;
        break;

    case LC_STATUS_STOPPED:
        strStatusName = "已停止";
        running = 0;
        break;
    }


    m_staStatus.SetWindowText(strStatusName);
    m_edtErrorDetail.SetWindowText(lc_get_error_message(code));

    m_BtnStart.EnableWindow(!running);
    m_BtnStop.EnableWindow(running);
}

// 采集对象状态回调
void LC_API status_callback(lc_handle_t handle, LC_STATUS status, LC_CODE msg_code,
                            void* user_data) {
    reinterpret_cast<CToolsDlg*>(user_data)->OnStatusChanged(status, msg_code);
}
void CToolsDlg::DockPlayer(CDialogPlayer* player) {

    CRect win;
    GetWindowRect(win);
    SetWindowPos(NULL, win.left, win.top, 1024, win.Height(), SWP_NOZORDER | SWP_NOREPOSITION);

    player->ModifyStyleEx(WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE, 0);
    player->ModifyStyle(WS_OVERLAPPEDWINDOW, WS_CHILD);
    player->SetParent(this);

    CRect rcClient;
    GetClientRect(rcClient);

    player->SetWindowPos(NULL, 520, 0, rcClient.Width() - 520, rcClient.Height(),
                         SWP_NOZORDER | SWP_FRAMECHANGED);
}

void CToolsDlg::UndockPlayer(CDialogPlayer* player) {
    player->SetParent(NULL);
    player->ModifyStyleEx(0, WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE);
    player->ModifyStyle(WS_CHILD, WS_OVERLAPPEDWINDOW, SWP_FRAMECHANGED);
    player->SetOwner(this);

    CRect win;
    GetWindowRect(win);

    MoveWindow(win.left, win.top, 520, win.Height());
}

LRESULT CToolsDlg::OnStatusChanged_WM(WPARAM wParam, LPARAM lParam) {
    UpdateStatusUi((LC_STATUS)lParam, (LC_CODE)wParam);
    return TRUE;
}

void CToolsDlg::OnStatusChanged(LC_STATUS status, LC_CODE code) {
    PostMessage(WM_USER_STATUS_CHANGED, code, status);
}

void LC_API CToolsDlg::sample_callback(lc_handle_t handle, LC_SAMPLE_TYPE type, void* sample,
                                       void* usr_data) {
    reinterpret_cast<CToolsDlg*>(usr_data)->OnSample(handle, type, sample);
}

void CToolsDlg::OnSample(lc_handle_t handle, LC_SAMPLE_TYPE type, void* sample) {

    if (handle == m_handle) {
        switch (type) {
        case LC_SAMPLE_RAW_VIDEO:
            m_wndPlayer.PushVideo((lc_sample_raw_video_t*)sample);
            break;

        case LC_SAMPLE_RAW_AUDIO:
            m_wndPlayer.PushAudio((lc_sample_raw_audio_t*)sample);
            break;

        default:
            break;
        }
    }
}

void CToolsDlg::OnBnClickedButtonStart() {
    if (m_handle) {
        lc_close(m_handle);
        m_handle = NULL;
    }

    UpdateStatus();
    UpdateData();

    // 判断用户输入是否合法
    if (ValidateData()) {

        // 保存用户配置
        m_Model.Serailize();
        lc_config_t config = {0};
        strncpy_s(config.video_inputs[0].device_name, m_Model.m_VideoDevice, -1);
        strncpy_s(config.audio_inputs[0].device_name, m_Model.m_AudioDevice, -1);

        int iInfo = m_cmbVideoInfos.GetCurSel();

        if (iInfo) {
            const lc_video_info_t* video_info =
                (const lc_video_info_t*)m_cmbVideoInfos.GetItemData(iInfo);

            if (video_info) {
                config.video_inputs[0].info = *video_info;
            }
        }

        int iPresetIndex = m_CmbPresets.GetCurSel();
        config.preset = *((lc_preset_t*)m_CmbPresets.GetItemData(iPresetIndex));

        if (m_Model.m_RtmpOption == CREATE_SESSION) {
            strncpy_s(config.session.presetName, m_Model.m_Preset, -1);
            strncpy_s(config.session.description, m_Model.m_BCESessionName, -1);

            strncpy_s(config.session.target.bosBucket, m_Model.m_BCEBucket, -1);
            strncpy_s(config.session.target.userDomain, m_Model.m_BCEUserDomain, -1);
        }

        LC_CODE ret = LC_OK;
        // 调用API, 创建采集对象
        m_handle = lc_create(&config, &ret);

        if (!m_handle) {
            MessageBox("创建采集对象失败", "失败", MB_OK);
            return ;
        } else {
            // 调用API, 注册采集对象状态回调函数
            lc_register_status_callback(m_handle, status_callback, this);
            lc_sample_set_callback(m_handle, sample_callback, LC_SAMPLE_RAW_AUDIO | LC_SAMPLE_RAW_VIDEO, this);
            m_wndPlayer.Reset();

            // 调用API, 启动采集
            ret = lc_start(m_handle,
                           m_Model.m_RtmpOption == EXISTING_SESSION ? m_Model.m_BCESessionId : NULL,
                           m_Model.m_RtmpOption == USER_RTMP ? m_Model.m_UserRTMPUrl : NULL,
                           m_Model.m_SaveLocal ? m_Model.m_LocalSavePath : NULL);

            if (ret == LC_OK) {
                // 启动一个Timer,用于更新状态，播放地址和码率信息
                m_uTimer = SetTimer(0x1, 1000, NULL);

                if (m_uTimer == 0) {
                    CLogMgr::Instance().AppendLog(LC_LOG_ERROR, "set timer failed. play url will not be updated.");
                }

                OnStart();
            }
        }

        UpdateStatus();
    }
}


void CToolsDlg::OnOK() {

}

void CToolsDlg::OnCancel() {

}

void CToolsDlg::OnBnClickedRadioExistingSession() {
    int nRtmpOption = GetCheckedRadioButton(IDC_RADIO_EXISTING_SESSION, IDC_RADIO_RTMP) -
                      IDC_RADIO_EXISTING_SESSION;
    UpdateRtmpRadio(nRtmpOption);
}

void CToolsDlg::OnBnClickedRadioRtmp() {
    int nRtmpOption = GetCheckedRadioButton(IDC_RADIO_EXISTING_SESSION, IDC_RADIO_RTMP) -
                      IDC_RADIO_EXISTING_SESSION;
    UpdateRtmpRadio(nRtmpOption);
}

void CToolsDlg::OnBnClickedRadioBce() {
    int nRtmpOption = GetCheckedRadioButton(IDC_RADIO_EXISTING_SESSION, IDC_RADIO_RTMP) -
                      IDC_RADIO_EXISTING_SESSION;
    UpdateRtmpRadio(nRtmpOption);
}

void CToolsDlg::UpdateRtmpRadio(int nRtmpOption) {
    CWnd* userRtmpCtls[] = {&m_EdtUserRtmp, 0};
    CWnd* bceRtmpCtls[] = {&m_EdtSessionName, &m_EdtBosBucket, &m_EdtUserDomain, 0};
    CWnd* existingSessionCtls[] = {&m_edtExistingSession, &m_btnSelectSession, 0};

    for (int i = 0; userRtmpCtls[i]; i ++) {
        userRtmpCtls[i]->EnableWindow(nRtmpOption == USER_RTMP);
    }

    for (int i = 0; bceRtmpCtls[i]; i ++) {
        bceRtmpCtls[i]->EnableWindow(nRtmpOption == CREATE_SESSION);
    }

    for (int i = 0; existingSessionCtls[i]; i ++) {
        existingSessionCtls[i]->EnableWindow(nRtmpOption == EXISTING_SESSION);
    }
}


void CToolsDlg::OnBnClickedCheckSaveLocal() {
    BOOL bCheck = m_ChkSaveLocal.GetCheck() && m_ChkSaveLocal.IsWindowEnabled();

    CWnd* ctrls[] = {&m_EdtLocalPath, &m_BtnSelectPath, 0};

    for (int i = 0 ; ctrls[i]; i ++) {
        ctrls[i]->EnableWindow(bCheck);
    }
}

void CToolsDlg::OnBnClickedButtonRefreshData() {
    UpdateLiveCaptureData();
}

void CToolsDlg::OnTimer(UINT_PTR nIDEvent) {
    if (m_handle) {

        lc_session_play_t play = { 0 };

        // 调用API, 得到播放地址
        if (LC_OK == lc_query_play_url(m_handle, &play)) {
            CString text;
            m_EdtHLSPlayUrl.GetWindowText(text);

            if (text.CompareNoCase(play.hlsUrl) != 0) {
                m_EdtHLSPlayUrl.SetWindowText(play.hlsUrl);
            }

            m_EdtRtmpPlayUrl.GetWindowText(text);

            if (text.CompareNoCase(play.rtmpUrl) != 0) {
                m_EdtRtmpPlayUrl.SetWindowText(play.rtmpUrl);
            }
        }

        lc_statistics_stream_t strm = {0 };

        // 调用API, 得到当前的码率信息
        if (LC_OK == lc_statistics_get(m_handle, &strm)) {
            CString strStatis;
            CString strBitrate;
            FriendlyBitrate(strBitrate, strm.bitrate_in_bps);
            strStatis.Format("码率:%s, 帧率:%.2f fps", (LPCSTR)strBitrate,
                             strm.video.framerate_x100 / 100.0f);

            m_edtStatis.SetWindowText(strStatis);
        }
    }
}

BOOL CToolsDlg::ValidateData() {
    CString err;

    if (!m_Model.Validate(err)) {
        MessageBox(err, "错误", MB_OK);
        return FALSE;
    }

    return TRUE;
}
void CToolsDlg::OnMenuPreset() {
    if (!m_pPresetDlg) {
        m_pPresetDlg = new CPresetDlg(this);
        m_pPresetDlg->Create(CPresetDlg::IDD, this);

        if (m_pPresetDlg->GetSafeHwnd()) {
            m_pPresetDlg->ShowWindow(SW_SHOW);
        } else {
            MessageBox("创建模板窗口失败", "错误", MB_OK | MB_ICONERROR);
        }
    } else {
        m_pPresetDlg->ShowWindow(SW_SHOW);
    }
}

void CToolsDlg::OnUpdateMenuPreset(CCmdUI* pCmdUI) {
    if (m_pPresetDlg) {
        pCmdUI->Enable(FALSE);
    } else {
        pCmdUI->Enable(TRUE);
    }
}

void CToolsDlg::OnMenuSession() {
    if (!m_pSessionDlg) {
        m_pSessionDlg = new CSessionDlg(this);
        m_pSessionDlg->Create(CSessionDlg::IDD, this);

        if (m_pSessionDlg->GetSafeHwnd()) {
            m_pSessionDlg->ShowWindow(SW_SHOW);
        } else {
            MessageBox("创建会话窗口失败", "错误", MB_OK | MB_ICONERROR);
        }
    } else {
        m_pSessionDlg->ShowWindow(SW_SHOW);
    }
}

void CToolsDlg::OnUpdateMenuSession(CCmdUI* pCmdUI) {
    if (m_pSessionDlg) {
        pCmdUI->Enable(FALSE);
    } else {
        pCmdUI->Enable(TRUE);
    }

}

void CToolsDlg::OnMenuLog() {
    if (!m_pLogDlg) {
        m_pLogDlg = new CLogDlg(this);
        m_pLogDlg->Create(CLogDlg::IDD, this);

        if (m_pLogDlg->GetSafeHwnd()) {
            m_pLogDlg->ShowWindow(SW_SHOW);
        } else {
            MessageBox("创建日志窗口失败", "错误", MB_OK | MB_ICONERROR);
        }
    } else {
        m_pLogDlg->ShowWindow(SW_SHOW);
    }
}

void CToolsDlg::OnUpdateMenuLog(CCmdUI* pCmdUI) {
    if (m_pLogDlg) {
        pCmdUI->Enable(FALSE);
    } else {
        pCmdUI->Enable(TRUE);
    }
}
void CToolsDlg::OnLogDlgClosed(CLogDlg* dlg) {
    if (dlg == m_pLogDlg) {
        delete m_pLogDlg ;
        m_pLogDlg = NULL;
    }
}
void CToolsDlg::OnPresetDlgClosed(CPresetDlg* dlg) {
    if (dlg == m_pPresetDlg) {
        delete m_pPresetDlg;
        m_pPresetDlg = NULL;
    }

}
void CToolsDlg::OnSessionDlgClosed(CSessionDlg* dlg) {
    if (dlg == m_pSessionDlg) {
        delete m_pSessionDlg;
        m_pSessionDlg = NULL;
    }
}
void CToolsDlg::EnableUI(BOOL enable) {
    CWnd* ctrls[] = {
        &m_BtnSelectPath,
        &m_CmbVideoDevices,
        &m_cmbVideoInfos,
        &m_CmbAudioDevices,
        &m_CmbPresets,
        &m_ChkSaveLocal,
        &m_EdtLocalPath,
        &m_EdtBosBucket,
        &m_EdtUserDomain,
        &m_EdtSessionName,
        &m_EdtUserRtmp,
        &m_rdoUserRtmp,
        &m_rdoExistRtmp,
        &m_edtExistingSession,
        &m_btnSelectSession,
        &m_rdoBceRtmp,
        &m_btnRefresh,

        NULL
    };

    CWnd** curWnd = ctrls;

    while (*curWnd) {
        (*curWnd)->EnableWindow(enable);
        curWnd++;
    }

    if (enable) {
        OnBnClickedCheckSaveLocal();
        OnBnClickedRadioRtmp();
    }
}
void CToolsDlg::OnStart() {
    EnableUI(FALSE);
}

void CToolsDlg::OnStop() {
    EnableUI(TRUE);
}
void CToolsDlg::OnDestroy() {
    OnBnClickedButtonStop();
    m_wndPlayer.DestroyWindow();
    CDialog::OnDestroy();
}

void CToolsDlg::OnBnClickedButtonSelectSession() {
    CSessionDlg dlg(this, TRUE);
    dlg.DoModal();
    CString id = dlg.GetSelectedSessionId();

    if (!id.IsEmpty()) {
        m_edtExistingSession.SetWindowText(id);
    }
}

void CToolsDlg::OnCbnSelchangeComboVideoInfo() {

}

LC_CODE LC_API CToolsDlg::enumVideoInfo(const lc_video_info_t* info, void* inst) {
    return reinterpret_cast<CToolsDlg*>(inst)->OnEnumVideoInfo(info);
}

LC_CODE CToolsDlg::OnEnumVideoInfo(const lc_video_info_t* info) {
    m_vecVideoInfos.push_back(*info);
    return LC_OK;
}

void CToolsDlg::UpdateVideoInfos() {
    int cur = m_CmbVideoDevices.GetCurSel();
    m_cmbVideoInfos.ResetContent();
    m_vecVideoInfos.clear();

    if (cur >= 0) {
        CString devName;
        m_CmbVideoDevices.GetLBText(cur, devName);
        // 调用API, 读取指定视频设备的支持的视频格式
        lc_video_device_enum_video_info(devName, enumVideoInfo, this);

        if (m_vecVideoInfos.size() > 0) {
            std::sort(m_vecVideoInfos.begin(), m_vecVideoInfos.end(), VideoInfoPred());

            for (std::vector<lc_video_info_t>::const_iterator it = m_vecVideoInfos.begin();
                    it != m_vecVideoInfos.end();
                    it ++) {
                int id = m_cmbVideoInfos.AddString(video_info_string(*it));

                if (id >= 0) {
                    m_cmbVideoInfos.SetItemData(id, (DWORD_PTR) & (*it));
                }
            }
        }
    }

    int id = m_cmbVideoInfos.AddString("默认");

    if (id >= 0) {
        m_cmbVideoInfos.SetCurSel(id);
    }
}

void CToolsDlg::OnCbnSelchangeComboVideo() {
    UpdateVideoInfos();
}

int CToolsDlg::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CDialog::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    if (!m_wndPlayer.Create(CDialogPlayer::IDD, this)) {
        return -1;
    }

    m_wndPlayer.SetHost(this);

    DockPlayer(&m_wndPlayer);
    m_wndPlayer.ShowWindow(SW_SHOW);
    return 0;
}
