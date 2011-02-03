#include "MainWindow.h"

#include <QtGlobal>
#include <QTimer>
#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QDir>
#include <QDebug>

const Int3D MainWindow::c_side_offset[] = {
    Int3D(1, 0, 0),
    Int3D(-1, 0, 0),
    Int3D(0, 1, 0),
    Int3D(0, -1, 0),
    Int3D(0, 0, 1),
    Int3D(0, 0, -1),
};

const Int3D MainWindow::c_chunk_size(16, 16, 128);

MainWindow::MainWindow() :
    m_root(NULL),
    m_camera(NULL),
    m_scene_manager(NULL),
    m_window(NULL),
    m_resources_config(Ogre::StringUtil::BLANK),
    m_camera_man(NULL),
    m_shut_down(false),
    m_input_manager(NULL),
    m_mouse(NULL),
    m_keyboard(NULL),
    m_server(NULL)
{
    loadControls();

    QUrl connection_settings;
    connection_settings.setHost("localhost");
    connection_settings.setPort(25565);
    connection_settings.setUserName("superbot");
    m_server = new Server(connection_settings);
    bool success;
    success = connect(m_server, SIGNAL(mapChunkUpdated(QSharedPointer<Chunk>)), this, SLOT(updateChunk(QSharedPointer<Chunk>)));
    Q_ASSERT(success);
    success = connect(m_server, SIGNAL(playerPositionUpdated(Server::EntityPosition)), this, SLOT(movePlayerPosition(Server::EntityPosition)));
    Q_ASSERT(success);
    m_server->socketConnect();
}

MainWindow::~MainWindow()
{
    delete m_camera_man;

    // Remove ourself as a Window listener
    Ogre::WindowEventUtilities::removeWindowEventListener(m_window, this);
    windowClosed(m_window);
    delete m_root;
}

void MainWindow::loadControls()
{
    QDir dir(QCoreApplication::applicationDirPath());
    QSettings settings(dir.absoluteFilePath("mineflayer.ini"), QSettings::IniFormat);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/forward", OIS::KC_W).toInt(), Forward);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/back", OIS::KC_S).toInt(), Back);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/left", OIS::KC_A).toInt(), Left);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/right", OIS::KC_D).toInt(), Right);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/jump", OIS::KC_SPACE).toInt(), Jump);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/crouch", OIS::KC_Z).toInt(), Crouch);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/discard_item", OIS::KC_Q).toInt(), DiscardItem);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/action_1", OIS::KC_NUMPAD0).toInt(), Action1);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/action_2", OIS::KC_NUMPAD1).toInt(), Action2);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/inventory", OIS::KC_I).toInt(), Inventory);
    m_key_to_control.insert((OIS::KeyCode)settings.value("controls/chat", OIS::KC_T).toInt(), Chat);

    QHashIterator<OIS::KeyCode, Control> it(m_key_to_control);
    while (it.hasNext()) {
        it.next();
        m_control_to_key.insert(it.value(), it.key());
    }
}

bool MainWindow::configure()
{
    if (! m_root->restoreConfig()) {
        if (! m_root->showConfigDialog())
            return false;
    }
    m_window = m_root->initialise(true, "Mine Flayer");
    if (! m_window)
        return false;

    return true;
}

void MainWindow::createCamera()
{
    // Create the camera
    m_camera = m_scene_manager->createCamera("PlayerCam");

    // Position it at 500 in Z direction
    m_camera->setPosition(Ogre::Vector3(0,0,0));
    // Look back along -Z
    m_camera->lookAt(Ogre::Vector3(1,1,-0.1));
    m_camera->setNearClipDistance(0.1);

    m_camera_man = new OgreBites::SdkCameraMan(m_camera);   // create a default camera controller
}

void MainWindow::createFrameListener()
{
    Ogre::LogManager::getSingletonPtr()->logMessage("*** Initializing OIS ***");
    OIS::ParamList pl;
    size_t windowHnd = 0;
    std::ostringstream windowHndStr;

    m_window->getCustomAttribute("WINDOW", &windowHnd);
    windowHndStr << windowHnd;
    pl.insert(std::make_pair(std::string("WINDOW"), windowHndStr.str()));
#ifdef OIS_LINUX_PLATFORM
   // pl.insert(std::make_pair(std::string("x11_mouse_grab"), std::string("false")));
    //pl.insert(std::make_pair(std::string("x11_keyboard_grab"), std::string("false")));
#endif

    m_input_manager = OIS::InputManager::createInputSystem( pl );

    m_keyboard = static_cast<OIS::Keyboard*>(m_input_manager->createInputObject( OIS::OISKeyboard, true ));
    m_mouse = static_cast<OIS::Mouse*>(m_input_manager->createInputObject( OIS::OISMouse, true ));

    m_mouse->setEventCallback(this);
    m_keyboard->setEventCallback(this);

    // Set initial mouse clipping size
    windowResized(m_window);

    // Register as a Window listener
    Ogre::WindowEventUtilities::addWindowEventListener(m_window, this);

    m_root->addFrameListener(this);
}

void MainWindow::createViewports()
{
    // Create one viewport, entire window
    Ogre::Viewport* vp = m_window->addViewport(m_camera);
    vp->setBackgroundColour(Ogre::ColourValue(0,0,0));

    // Alter the camera aspect ratio to match the viewport
    m_camera->setAspectRatio(
        Ogre::Real(vp->getActualWidth()) / Ogre::Real(vp->getActualHeight()));
}

void MainWindow::setupResources()
{
    Ogre::ResourceGroupManager * mgr = Ogre::ResourceGroupManager::getSingletonPtr();
    mgr->addResourceLocation("resources", "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, true);
}

void MainWindow::loadResources()
{
    Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

    // create the terrain material
    //Ogre::ResourceGroupManager::getSingleton().createResourceGroup("Global");
    Ogre::MaterialPtr lMaterial = Ogre::MaterialManager::getSingleton().create("Terrain", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    Ogre::Technique* lFirstTechnique = lMaterial->getTechnique(0);
    Ogre::Pass* lFirstPass = lFirstTechnique->getPass(0);

    lFirstPass->setLightingEnabled(true);
    Ogre::ColourValue lAmbientColour(0.5f, 0.5f, 0.5f, 1.0f);
    lFirstPass->setAmbient(lAmbientColour);

    Ogre::TextureUnitState* lTextureUnit = lFirstPass->createTextureUnitState();
    lTextureUnit->setTextureName("terrain.png", Ogre::TEX_TYPE_2D);
    lTextureUnit->setTextureCoordSet(0);

    {
        // grab all the textures from resources
        //QImage terrain(":/textures/terrain.png");
        //terrain.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        QFile texture_index_file(":/textures/textures.txt");
        texture_index_file.open(QFile::ReadOnly);
        QTextStream stream(&texture_index_file);
        while (! stream.atEnd()) {
            QString line = stream.readLine().trimmed();
            if (line.isEmpty() || line.startsWith("#"))
                continue;
            QStringList parts = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
            Q_ASSERT(parts.size() == 5);
            QString name = parts.at(0);
            int x = parts.at(1).toInt();
            int y = parts.at(2).toInt();
            int w = parts.at(3).toInt();
            int h = parts.at(4).toInt();
            //QImage image = terrain.copy(x, y, w, h).rgbSwapped();
            //Texture * texture = new Texture(image);
            //s_textures.insert(name, texture);
        }
        texture_index_file.close();
    }

    {
        // grab all the solid block data from resources
        QFile blocks_file(":/textures/blocks.txt");
        blocks_file.open(QFile::ReadOnly);
        QTextStream stream(&blocks_file);
        while(! stream.atEnd()) {
            QString line = stream.readLine().trimmed();
            if (line.isEmpty() || line.startsWith("#"))
                continue;
            QStringList parts = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
            Q_ASSERT(parts.size() == 8);
            int id = parts.at(0).toInt();
            QString name = parts.at(1);
            QString top = parts.at(2);
            QString bottom = parts.at(3);
            QString front = parts.at(4);
            QString back = parts.at(5);
            QString left = parts.at(6);
            QString right = parts.at(7);

//            Mesh * textured_cube = Mesh::createUnitCube(Constants::white,
//                s_textures.value(top),
//                s_textures.value(bottom),
//                s_textures.value(front),
//                s_textures.value(back),
//                s_textures.value(left),
//                s_textures.value(right));
//            s_meshes.replace(id, textured_cube);
        }
        blocks_file.close();
    }
}

void MainWindow::go()
{
    m_resources_config = "resources.cfg";

    if (!setup())
        return;

    m_root->startRendering();
}

bool MainWindow::setup()
{
    // suppress debug output on stdout
    Ogre::LogManager * logManager = new Ogre::LogManager;
    logManager->createLog("ogre.log", true, false, false);

    m_root = new Ogre::Root("resources/plugins.cfg");

    setupResources();

    bool carryOn = configure();
    if (!carryOn) return false;

    // Get the SceneManager, in this case a generic one
    m_scene_manager = m_root->createSceneManager(Ogre::ST_GENERIC);

    createCamera();
    createViewports();

    // Set default mipmap level (NB some APIs ignore this)
    Ogre::TextureManager::getSingleton().setDefaultNumMipmaps(5);

    // Load resources
    loadResources();

    // create the scene
    m_scene_manager->setAmbientLight(Ogre::ColourValue(0.5, 0.5, 0.5));

    createFrameListener();

    return true;
};

bool MainWindow::frameRenderingQueued(const Ogre::FrameEvent& evt)
{
    if(m_window->isClosed())
        return false;

    if(m_shut_down)
        return false;

    //Need to capture/update each device
    m_keyboard->capture();
    m_mouse->capture();
    QCoreApplication::processEvents();

    // compute next frame


    m_camera_man->frameRenderingQueued(evt);   // if dialog isn't up, then update the camera


    return true;
}

bool MainWindow::keyPressed( const OIS::KeyEvent &arg )
{
    if (arg.key == OIS::KC_ESCAPE || (m_keyboard->isModifierDown(OIS::Keyboard::Alt) && arg.key == OIS::KC_F4))
        m_shut_down = true;

    m_camera_man->injectKeyDown(arg);
    return true;
}

bool MainWindow::keyReleased( const OIS::KeyEvent &arg )
{
    m_camera_man->injectKeyUp(arg);
    return true;
}

bool MainWindow::mouseMoved( const OIS::MouseEvent &arg )
{
    m_camera_man->injectMouseMove(arg);
    return true;
}

bool MainWindow::mousePressed( const OIS::MouseEvent &arg, OIS::MouseButtonID id )
{
    m_camera_man->injectMouseDown(arg, id);
    return true;
}

bool MainWindow::mouseReleased( const OIS::MouseEvent &arg, OIS::MouseButtonID id )
{
    m_camera_man->injectMouseUp(arg, id);
    return true;
}

void MainWindow::windowResized(Ogre::RenderWindow* rw)
{
    // Adjust mouse clipping area
    unsigned int width, height, depth;
    int left, top;
    rw->getMetrics(width, height, depth, left, top);

    const OIS::MouseState &ms = m_mouse->getMouseState();
    ms.width = width;
    ms.height = height;
}

void MainWindow::windowClosed(Ogre::RenderWindow* rw)
{
    // Unattach OIS before window shutdown (very important under Linux)
    // Only close for window that created OIS (the main window)
    if (rw == m_window && m_input_manager) {
        m_input_manager->destroyInputObject(m_mouse);
        m_input_manager->destroyInputObject(m_keyboard);

        OIS::InputManager::destroyInputSystem(m_input_manager);
        m_input_manager = NULL;
    }
}

void MainWindow::updateChunk(QSharedPointer<Chunk> update)
{
    // build a mesh for the chunk
    // find the chunk coordinates for this updated stuff.
    Int3D chunk_key = chunkKey(update->position());

    // update our chunk with update
    QSharedPointer<Chunk> chunk = getChunk(chunk_key);
    Int3D offset;
    Int3D size = update.data()->size();
    for (offset.x = 0; offset.x < size.x; offset.x++) {
        for (offset.y = 0; offset.y < size.y; offset.y++) {
            for (offset.z = 0; offset.z < size.z; offset.z++) {
                chunk.data()->setBlock(update.data()->position() + offset - chunk.data()->position(), update.data()->getBlock(offset));
            }
        }
    }

    generateChunkMesh(chunk);
}

void MainWindow::generateChunkMesh(QSharedPointer<Chunk> chunk)
{
    // delete old mesh
    // TODO

    Ogre::ManualObject * obj = new Ogre::ManualObject(Ogre::String());
    obj->begin("Terrain", Ogre::RenderOperation::OT_TRIANGLE_LIST);

    Int3D offset;
    Int3D size = chunk.data()->size();
    for (offset.x = 0; offset.x < size.x; offset.x++) {
        for (offset.y = 0; offset.y < size.y; offset.y++) {
            for (offset.z = 0; offset.z < size.z; offset.z++) {
                Chunk::Block block = chunk.data()->getBlock(offset);

                if (block.type == Chunk::Air)
                    continue;

                // for every side
                for (int i = 0; i <= 6; i++) {
                    // if the block on this side is opaque, skip
                    Chunk::ItemType side_type = blockTypeAt(chunk.data()->position()+offset+c_side_offset[i]);
                    if (side_type != Chunk::Air)
                        continue;

                    // add this side to mesh
                    Int3D abs_block_loc = chunk.data()->position() + offset;
                    switch (i) {
                    case 0: // Int3D(1, 0, 0),
                        obj->position(abs_block_loc.x+1, abs_block_loc.y+0, abs_block_loc.z+1);
                        obj->textureCoord(32 / 256.0f, 0 / 256.0f);

                        obj->position(abs_block_loc.x+1, abs_block_loc.y+0, abs_block_loc.z+0);
                        obj->textureCoord(32 / 256.0f, 16 / 256.0f);

                        obj->position(abs_block_loc.x+1, abs_block_loc.y+1, abs_block_loc.z+1);
                        obj->textureCoord(48 / 256.0f, 0 / 256.0f);
                        //qDebug() << "triangle at " << abs_block_loc.x << abs_block_loc.y << abs_block_loc.z;
                        break;
                    case 1: // Int3D(-1, 0, 0),
                        break;
                    case 2: // Int3D(0, 1, 0),
                        break;
                    case 3: // Int3D(0, -1, 0),
                        break;
                    case 4: // Int3D(0, 0, 1),
                        break;
                    case 5: // Int3D(0, 0, -1),
                        break;
                    }

                }
            }
        }
    }
    obj->end();

    Ogre::SceneNode * node = m_scene_manager->getRootSceneNode();
    Ogre::SceneNode * chunk_node = node->createChildSceneNode();
    chunk_node->attachObject(obj);
}

Chunk::ItemType MainWindow::blockTypeAt(const Int3D & coord)
{
    Int3D chunk_key = chunkKey(coord);
    QSharedPointer<Chunk> chunk = m_chunks.value(chunk_key, QSharedPointer<Chunk>());
    if (chunk.isNull())
        return Chunk::Air;
    Int3D offset = coord - chunk_key;
    return chunk.data()->getBlock(offset).type;
}

Int3D MainWindow::chunkKey(const Int3D & coord)
{
    return coord - (coord % c_chunk_size);
}

QSharedPointer<Chunk> MainWindow::getChunk(const Int3D & key)
{
    QSharedPointer<Chunk> chunk = m_chunks.value(key, QSharedPointer<Chunk>());
    if (! chunk.isNull())
        return chunk;
    chunk = QSharedPointer<Chunk>(new Chunk(key, c_chunk_size));
    m_chunks.insert(key, chunk);
    return chunk;
}

void MainWindow::movePlayerPosition(Server::EntityPosition position)
{
    m_camera->setPosition(Ogre::Vector3(position.x, position.y, position.z));
}