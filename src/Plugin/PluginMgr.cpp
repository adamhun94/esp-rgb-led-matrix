/* MIT License
 *
 * Copyright (c) 2019 - 2023 Andreas Merkle <web@blue-andi.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*******************************************************************************
    DESCRIPTION
*******************************************************************************/
/**
 * @brief  Plugin manager
 * @author Andreas Merkle <web@blue-andi.de>
 */

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "PluginMgr.h"
#include "DisplayMgr.h"
#include "MyWebServer.h"
#include "RestApi.h"
#include "Settings.h"
#include "FileSystem.h"
#include "Plugin.hpp"
#include "HttpStatus.h"
#include "RestUtil.h"
#include "JsonFile.h"

#include <Logging.h>
#include <ArduinoJson.h>
#include <Util.h>

/******************************************************************************
 * Compiler Switches
 *****************************************************************************/

/******************************************************************************
 * Macros
 *****************************************************************************/

/******************************************************************************
 * Types and classes
 *****************************************************************************/

/******************************************************************************
 * Prototypes
 *****************************************************************************/

/******************************************************************************
 * Local Variables
 *****************************************************************************/

/* Initialize the full filename of the configuration file. */
const char* PluginMgr::CONFIG_FILE_NAME = "slotConfig.json";

/******************************************************************************
 * Public Methods
 *****************************************************************************/

void PluginMgr::begin()
{
    createPluginConfigDirectory();
}

void PluginMgr::registerPlugin(const String& name, IPluginMaintenance::CreateFunc createFunc)
{
    m_pluginFactory.registerPlugin(name, createFunc);
    return;
}

IPluginMaintenance* PluginMgr::install(const String& name, uint8_t slotId)
{
    IPluginMaintenance* plugin = m_pluginFactory.createPlugin(name);

    if (nullptr != plugin)
    {
        if (false == install(plugin, slotId))
        {
            m_pluginFactory.destroyPlugin(plugin);
            plugin = nullptr;
        }
    }

    return plugin;
}

bool PluginMgr::uninstall(IPluginMaintenance* plugin)
{
    bool status = false;

    if (nullptr != plugin)
    {
        status = DisplayMgr::getInstance().uninstallPlugin(plugin);

        if (true == status)
        {
            unregisterTopics(plugin);
            m_pluginFactory.destroyPlugin(plugin);
        }
    }

    return status;
}

const char* PluginMgr::findFirst()
{
    return m_pluginFactory.findFirst();
}

const char* PluginMgr::findNext()
{
    return m_pluginFactory.findNext();
}

bool PluginMgr::setPluginAliasName(IPluginMaintenance* plugin, const String& alias)
{
    bool isSuccessful = false;

    if ((nullptr != plugin) &&
        (plugin->getAlias() != alias))
    {
        /* First remove current registered topics. */
        unregisterTopics(plugin);

        /* Set new alias */
        plugin->setAlias(alias);

        /* Register web API, based on new alias. */
        registerTopics(plugin);

        isSuccessful = true;
    }

    return isSuccessful;
}

String PluginMgr::getRestApiBaseUriByUid(uint16_t uid)
{
    String  baseUri = RestApi::BASE_URI;
    baseUri += "/display";
    baseUri += "/uid/";
    baseUri += uid;

    return baseUri;
}

String PluginMgr::getRestApiBaseUriByAlias(const String& alias)
{
    String  baseUri = RestApi::BASE_URI;
    baseUri += "/display";
    baseUri += "/alias/";
    baseUri += alias;

    return baseUri;
}

bool PluginMgr::load()
{
    bool                isSuccessful            = true;
    JsonFile            jsonFile(FILESYSTEM);
    const size_t        JSON_DOC_SIZE           = 4096U;
    DynamicJsonDocument jsonDoc(JSON_DOC_SIZE);
    String              fullConfigFileName      = Plugin::CONFIG_PATH;

    fullConfigFileName += "/";
    fullConfigFileName += CONFIG_FILE_NAME;

    if (false == jsonFile.load(fullConfigFileName, jsonDoc))
    {
        LOG_WARNING("Failed to load file %s.", fullConfigFileName.c_str());
        isSuccessful = false;
    }
    else
    {
        JsonArray       jsonSlots   = jsonDoc["slotConfiguration"].as<JsonArray>();
        uint8_t         slotId      = 0;
        const uint8_t   MAX_SLOTS   = DisplayMgr::getInstance().getMaxSlots();

        checkJsonDocOverflow(jsonDoc, __LINE__);

        for(JsonObject jsonSlot: jsonSlots)
        {
            prepareSlotByConfiguration(slotId, jsonSlot);
            
            ++slotId;
            if (MAX_SLOTS <= slotId)
            {
                break;
            }
        }
    }

    return isSuccessful;
}

void PluginMgr::save()
{
    String              installation;
    uint8_t             slotId              = 0;
    const size_t        JSON_DOC_SIZE       = 4096U;
    DynamicJsonDocument jsonDoc(JSON_DOC_SIZE);
    JsonArray           jsonSlots           = jsonDoc.createNestedArray("slotConfiguration");
    JsonFile            jsonFile(FILESYSTEM);
    String              fullConfigFileName  = Plugin::CONFIG_PATH;

    fullConfigFileName += "/";
    fullConfigFileName += CONFIG_FILE_NAME;

    for(slotId = 0; slotId < DisplayMgr::getInstance().getMaxSlots(); ++slotId)
    {
        IPluginMaintenance* plugin      = DisplayMgr::getInstance().getPluginInSlot(slotId);
        JsonObject          jsonSlot    = jsonSlots.createNestedObject();

        if (nullptr == plugin)
        {
            jsonSlot["name"]        = "";
            jsonSlot["uid"]         = 0;
            jsonSlot["alias"]       = "";
            jsonSlot["fontType"]    = Fonts::fontTypeToStr(Fonts::FONT_TYPE_DEFAULT);
            jsonSlot["duration"]    = DisplayMgr::getInstance().getSlotDuration(slotId);
        }
        else
        {
            jsonSlot["name"]        = plugin->getName();
            jsonSlot["uid"]         = plugin->getUID();
            jsonSlot["alias"]       = plugin->getAlias();
            jsonSlot["fontType"]    = Fonts::fontTypeToStr(plugin->getFontType());
            jsonSlot["duration"]    = DisplayMgr::getInstance().getSlotDuration(slotId);
        }
    }

    checkJsonDocOverflow(jsonDoc, __LINE__);

    if (false == jsonFile.save(fullConfigFileName, jsonDoc))
    {
        LOG_ERROR("Couldn't save slot configuration.");
    }
}

/******************************************************************************
 * Protected Methods
 *****************************************************************************/

/******************************************************************************
 * Private Methods
 *****************************************************************************/

/**
 * Check dynamic JSON document for overflow and log a corresponding message,
 * otherwise log its document size.
 * 
 * @param[in] jsonDoc   Dynamic JSON document, which to check.
 * @param[in] line      Line number where the document is handled in the module.
 */
void PluginMgr::checkJsonDocOverflow(const DynamicJsonDocument& jsonDoc, int line)
{
    if (true == jsonDoc.overflowed())
    {
        LOG_ERROR("JSON document @%d has less memory available.", line);
    }
    else
    {
        LOG_INFO("JSON document @%d size: %u", line, jsonDoc.memoryUsage());
    }
}

void PluginMgr::createPluginConfigDirectory()
{
    if (false == FILESYSTEM.exists(Plugin::CONFIG_PATH))
    {
        if (false == FILESYSTEM.mkdir(Plugin::CONFIG_PATH))
        {
            LOG_WARNING("Couldn't create directory: %s", Plugin::CONFIG_PATH);
        }
    }
}

void PluginMgr::prepareSlotByConfiguration(uint8_t slotId, const JsonObject& jsonSlot)
{
    bool                isKeyValuePairMissing   = false;
    JsonVariantConst    jsonName                = jsonSlot["name"];
    JsonVariantConst    jsonUid                 = jsonSlot["uid"];
    JsonVariantConst    jsonAlias               = jsonSlot["alias"];
    JsonVariantConst    jsonFontType            = jsonSlot["fontType"];
    JsonVariantConst    jsonDuration            = jsonSlot["duration"];

    if (false == jsonName.is<String>())
    {
        LOG_WARNING("Slot %u: Name is missing.", slotId);
        isKeyValuePairMissing = true;
    }

    if (false == jsonUid.is<uint16_t>())
    {
        LOG_WARNING("Slot %u: UID is missing.", slotId);
        isKeyValuePairMissing = true;
    }

    if (false == jsonAlias.is<String>())
    {
        LOG_WARNING("Slot %u: Alias is missing.", slotId);
        isKeyValuePairMissing = true;
    }

    if (false == jsonFontType.is<String>())
    {
        LOG_WARNING("Slot %u: Font type is missing.", slotId);
        isKeyValuePairMissing = true;
    }

    if (false == jsonDuration.is<uint32_t>())
    {
        LOG_WARNING("Slot %u: Slot duration is missing.", slotId);
        isKeyValuePairMissing = true;
    }

    if (false == isKeyValuePairMissing)
    {
        String      name        = jsonName.as<String>();
        uint32_t    duration    = jsonDuration.as<uint32_t>();

        if (false == name.isEmpty())
        {
            IPluginMaintenance* plugin = DisplayMgr::getInstance().getPluginInSlot(slotId);

            /* If there is already a plugin installed, it may be a system plugin.
             * In this case skip it, otherwise continue.
             */
            if (nullptr == plugin)
            {
                uint16_t uid = jsonUid.as<uint16_t>();

                plugin = m_pluginFactory.createPlugin(name, uid);
            
                if (nullptr == plugin)
                {
                    LOG_ERROR("Couldn't create plugin %s (uid %u) in slot %u.", name.c_str(), uid, slotId);
                }
                else
                {
                    String          alias       = jsonAlias.as<String>();
                    String          fontTypeStr = jsonFontType.as<String>();
                    Fonts::FontType fontType    = Fonts::strToFontType(fontTypeStr.c_str());

                    plugin->setAlias(alias);
                    plugin->setFontType(fontType);

                    if (false == install(plugin, slotId))
                    {
                        LOG_WARNING("Couldn't install %s (uid %u) in slot %u.", name.c_str(), uid, slotId);

                        m_pluginFactory.destroyPlugin(plugin);
                        plugin = nullptr;
                    }
                    else
                    {
                        plugin->enable();
                    }
                }
            }
        }

        DisplayMgr::getInstance().setSlotDuration(slotId, duration);
    }
}

bool PluginMgr::install(IPluginMaintenance* plugin, uint8_t slotId)
{
    bool isSuccessful = false;

    if (nullptr != plugin)
    {
        if (SlotList::SLOT_ID_INVALID == slotId)
        {
            isSuccessful = installToAutoSlot(plugin);
        }
        else
        {
            isSuccessful = installToSlot(plugin, slotId);
        }

        if (true == isSuccessful)
        {
            registerTopics(plugin);
        }
    }

    return isSuccessful;
}

bool PluginMgr::installToAutoSlot(IPluginMaintenance* plugin)
{
    bool status = false;

    if (nullptr != plugin)
    {
        if (SlotList::SLOT_ID_INVALID == DisplayMgr::getInstance().installPlugin(plugin))
        {
            LOG_ERROR("Couldn't install plugin %s.", plugin->getName());
        }
        else
        {
            status = true;
        }
    }

    return status;
}

bool PluginMgr::installToSlot(IPluginMaintenance* plugin, uint8_t slotId)
{
    bool status = false;

    if (nullptr != plugin)
    {
        if (SlotList::SLOT_ID_INVALID == DisplayMgr::getInstance().installPlugin(plugin, slotId))
        {
            LOG_ERROR("Couldn't install plugin %s to slot %u.", plugin->getName(), slotId);
        }
        else
        {
            status = true;
        }
    }

    return status;
}

void PluginMgr::registerTopics(IPluginMaintenance* plugin)
{
    if (nullptr != plugin)
    {
        const size_t        JSON_DOC_SIZE   = 512U;
        DynamicJsonDocument topicsDoc(JSON_DOC_SIZE);
        JsonArray           topics          = topicsDoc.createNestedArray("topics");

        /* Get topics from plugin. */
        plugin->getTopics(topics);

        /* Handle each topic */
        if (0U < topics.size())
        {
            PluginObjData*  metaData = new(std::nothrow) PluginObjData();

            if (nullptr != metaData)
            {
                String baseUriByUid     = getRestApiBaseUriByUid(plugin->getUID());
                String baseUriByAlias;

                if (false == plugin->getAlias().isEmpty())
                {
                    baseUriByAlias = getRestApiBaseUriByAlias(plugin->getAlias());
                }

                metaData->plugin = plugin;

                for (JsonVariantConst topic : topics)
                {
                    registerTopic(baseUriByUid, metaData, topic.as<String>());

                    if (false == baseUriByAlias.isEmpty())
                    {
                        registerTopic(baseUriByAlias, metaData, topic.as<String>());
                    }
                }

                if (false == m_pluginMeta.append(metaData))
                {
                    LOG_WARNING("Couldn't append plugin meta data.");
                }
            }
        }
    }
}

void PluginMgr::registerTopic(const String& baseUri, PluginObjData* metaData, const String& topic)
{
    String          topicUri        = baseUri + topic;
    uint8_t         idx             = 0U;
    WebHandlerData* webHandlerData  = nullptr;

    /* Find empty web handler slot */
    for(idx = 0U; idx < PluginObjData::MAX_WEB_HANDLERS; ++idx)
    {
        if (nullptr == metaData->webHandlers[idx].webHandler)
        {
            webHandlerData = &metaData->webHandlers[idx];
            break;
        }
    }

    if (nullptr == webHandlerData)
    {
        LOG_WARNING("[%s][%u] No web handler available anymore.", metaData->plugin->getName(), metaData->plugin->getUID());
    }
    else
    {
        IPluginMaintenance* plugin = metaData->plugin;

        webHandlerData->webHandler  = &MyWebServer::getInstance().on(
                                        topicUri.c_str(),
                                        HTTP_ANY,
                                        [this, plugin, topic, webHandlerData](AsyncWebServerRequest *request)
                                        {
                                            this->webReqHandler(request, plugin, topic, webHandlerData);
                                        },
                                        [this, plugin, topic, webHandlerData](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
                                        {
                                            this->uploadHandler(request, filename, index, data, len, final, plugin, topic, webHandlerData);
                                        });
        webHandlerData->uri         = topicUri;

        LOG_INFO("[%s][%u] Register: %s", metaData->plugin->getName(), metaData->plugin->getUID(), topicUri.c_str());
    }
}

void PluginMgr::webReqHandler(AsyncWebServerRequest *request, IPluginMaintenance* plugin, const String& topic, WebHandlerData* webHandlerData)
{
    String              content;
    const size_t        JSON_DOC_SIZE   = 1024U;
    DynamicJsonDocument jsonDoc(JSON_DOC_SIZE);
    JsonObject          dataObj         = jsonDoc.createNestedObject("data");
    uint32_t            httpStatusCode  = HttpStatus::STATUS_CODE_OK;

    if ((nullptr == request) ||
        (nullptr == plugin) ||
        (nullptr == webHandlerData))
    {
        return;
    }

    if (HTTP_GET == request->method())
    {
        if (false == plugin->getTopic(topic, dataObj))
        {
            RestUtil::prepareRspError(jsonDoc, "Requested topic not supported.");

            jsonDoc.remove("data");

            httpStatusCode = HttpStatus::STATUS_CODE_NOT_FOUND;
        }
        else
        {
            jsonDoc["status"]   = "ok";
            httpStatusCode      = HttpStatus::STATUS_CODE_OK;
        }
    }
    else if (HTTP_POST == request->method())
    {
        DynamicJsonDocument jsonDocPar(JSON_DOC_SIZE);
        size_t              idx = 0U;

        /* Add arguments */
        for(idx = 0U; idx < request->args(); ++idx)
        {
            jsonDocPar[request->argName(idx)] = request->arg(idx);
        }

        /* Add uploaded file */
        if ((false == webHandlerData->isUploadError) &&
            (false == webHandlerData->fullPath.isEmpty()))
        {
            jsonDocPar["fullPath"] = webHandlerData->fullPath;
        }

        if (false == plugin->setTopic(topic, jsonDocPar.as<JsonObject>()))
        {
            RestUtil::prepareRspError(jsonDoc, "Requested topic not supported or invalid data.");

            jsonDoc.remove("data");

            /* If a file is available, it will be removed now. */
            if (false == webHandlerData->fullPath.isEmpty())
            {
                (void)FILESYSTEM.remove(webHandlerData->fullPath);
            }

            httpStatusCode = HttpStatus::STATUS_CODE_NOT_FOUND;
        }
        else
        {
            jsonDoc["status"]   = "ok";
            httpStatusCode      = HttpStatus::STATUS_CODE_OK;
        }
    }
    else
    {
        RestUtil::prepareRspErrorHttpMethodNotSupported(jsonDoc);

        jsonDoc.remove("data");

        httpStatusCode = HttpStatus::STATUS_CODE_NOT_FOUND;
    }

    RestUtil::sendJsonRsp(request, jsonDoc, httpStatusCode);

    return;
}

void PluginMgr::uploadHandler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final, IPluginMaintenance* plugin, const String& topic, WebHandlerData* webHandlerData)
{
    /* Begin of upload? */
    if (0 == index)
    {
        AsyncWebHeader* headerXFileSize = request->getHeader("X-File-Size");
        size_t          fileSize        = request->contentLength();
        size_t          fileSystemSpace = FILESYSTEM.totalBytes() - FILESYSTEM.usedBytes();

        /* File size available? */
        if (nullptr != headerXFileSize)
        {
            uint32_t u32FileSize = 0U;

            if (true == Util::strToUInt32(headerXFileSize->value(), u32FileSize))
            {
                fileSize = u32FileSize;
            }
        }

        if (fileSystemSpace <= fileSize)
        {
            LOG_WARNING("Upload of %s aborted. Not enough space.", filename.c_str());
            webHandlerData->isUploadError = true;
            webHandlerData->fullPath.clear();
        }
        else
        {
            LOG_INFO("Upload of %s (%d bytes) starts.", filename.c_str(), fileSize);
            webHandlerData->isUploadError = false;
            webHandlerData->fullPath.clear();

            /* Ask plugin, whether the upload is allowed or not. */
            if (false == plugin->isUploadAccepted(topic, filename, webHandlerData->fullPath))
            {
                LOG_WARNING("[%s][%u] Upload not supported.", plugin->getName(), plugin->getUID());
                webHandlerData->isUploadError = true;
                webHandlerData->fullPath.clear();
            }
            else
            {
                /* Create a new file and overwrite a existing one. */
                webHandlerData->fd = FILESYSTEM.open(webHandlerData->fullPath, "w");

                if (false == webHandlerData->fd)
                {
                    LOG_ERROR("Couldn't create file: %s", webHandlerData->fullPath.c_str());
                    webHandlerData->isUploadError = true;
                    webHandlerData->fullPath.clear();
                }
            }
        }
    }

    if (false == webHandlerData->isUploadError)
    {
        /* If file is open, write data to it. */
        if (true == webHandlerData->fd)
        {
            if (len != webHandlerData->fd.write(data, len))
            {
                LOG_ERROR("Less data written, upload aborted.");
                webHandlerData->isUploadError = true;
                webHandlerData->fullPath.clear();
                webHandlerData->fd.close();
            }
        }

        /* Upload finished? */
        if (true == final)
        {
            LOG_INFO("Upload of %s finished.", filename.c_str());

            webHandlerData->fd.close();
        }
    }

    return;
}

void PluginMgr::unregisterTopics(IPluginMaintenance* plugin)
{
    if (nullptr != plugin)
    {
        DLinkedListIterator<PluginObjData*> it(m_pluginMeta);

        /* Walk through plugin meta and remove every topic.
         * At the end, destroy the meta information.
         */
        if (true == it.first())
        {
            PluginObjData*  pluginMeta  = *it.current();
            bool            isFound     = false;

            while((false == isFound) && (nullptr != pluginMeta))
            {
                if (plugin == pluginMeta->plugin)
                {
                    isFound = true;
                }
                else if (false == it.next())
                {
                    pluginMeta = nullptr;
                }
                else
                {
                    pluginMeta = *it.current();
                }
            }

            if ((true == isFound) &&
                (nullptr != pluginMeta))
            {
                uint8_t idx = 0U;

                for(idx = 0U; idx < PluginObjData::MAX_WEB_HANDLERS; ++idx)
                {
                    if (nullptr != pluginMeta->webHandlers[idx].webHandler)
                    {
                        LOG_INFO("[%s][%u] Unregister: %s", pluginMeta->plugin->getName(), pluginMeta->plugin->getUID(), pluginMeta->webHandlers[idx].uri.c_str());

                        if (false == MyWebServer::getInstance().removeHandler(pluginMeta->webHandlers[idx].webHandler))
                        {
                            LOG_WARNING("Couldn't remove handler %u.", idx);
                        }

                        pluginMeta->webHandlers[idx].webHandler = nullptr;
                    }
                }

                it.remove();
                delete pluginMeta;
            }
        }
    }
}

/******************************************************************************
 * External Functions
 *****************************************************************************/

/******************************************************************************
 * Local Functions
 *****************************************************************************/
