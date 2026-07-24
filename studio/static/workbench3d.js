import * as THREE from "three";
import { STLLoader } from "three/addons/loaders/STLLoader.js";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";

const MODEL_BASE = new URL("./models/", import.meta.url).href;
const MODEL_FILES = {
  case: "Lemon-box-with-screen.stl",
  lid: "Lemon-box-lid-new.stl",
  logo: "Lemon-box-lid-logo-new.stl",
};

const SCREEN_PLANE_SLOPE_Z = -0.26794917379834526;
const SCREEN_PLANE_INTERCEPT = -104.11263554998368;
const SCREEN_PLANE_LIFT = 0.32;

const EXPLODED = {
  case: new THREE.Vector3(0, 0, 0),
  lid: new THREE.Vector3(0, -75, 0),
  logo: new THREE.Vector3(0, -132, 20),
};

export function createWorkbench3D(container, screenCanvas, options = {}) {
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(36, 1, 0.1, 5000);
  camera.position.set(0, 18, -120);

  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: "high-performance" });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.08;
  container.replaceChildren(renderer.domElement);

  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.06;
  controls.enablePan = false;
  controls.minDistance = 55;
  controls.maxDistance = 190;
  controls.target.set(0, 0, 0);

  scene.add(new THREE.AmbientLight(0xffffff, 0.62));
  addLight(scene, 0xffffff, 2.15, 60, 100, 60);
  addLight(scene, 0x00f068, 0.48, -70, 30, -50);
  addLight(scene, 0xdde5ff, 0.64, -40, -30, 60);
  scene.environment = new THREE.PMREMGenerator(renderer).fromScene(new RoomEnvironment(), 0.04).texture;

  const group = new THREE.Group();
  scene.add(group);

  const texture = new THREE.CanvasTexture(screenCanvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 8;

  const state = {
    ready: false,
    progress: 1,
    cameraTween: null,
    home: {},
    meshes: {},
    screenWorld: new THREE.Vector3(0, 0, 0),
    screenBasis: {
      normal: new THREE.Vector3(0, 0, -1),
      right: new THREE.Vector3(1, 0, 0),
      up: new THREE.Vector3(0, 1, 0),
    },
  };

  loadModels(group, texture, state)
    .then(() => {
      state.ready = true;
      setAssemblyProgress(state.progress);
      orientScreenNormalToCamera();
      setCameraView("front");
      if (options.onReady) options.onReady();
    })
    .catch((err) => {
      container.dataset.error = err.message || String(err);
      if (options.onError) options.onError(err);
    });

  const ro = new ResizeObserver(resize);
  ro.observe(container);
  resize();
  animate();

  function updateTexture() {
    texture.needsUpdate = true;
  }

  function setAssemblyProgress(value) {
    state.progress = clamp01(value);
    if (!state.ready) return;
    state.cameraTween = null;
    const t = easeInOutCubic(state.progress);
    for (const name of Object.keys(state.meshes)) {
      const mesh = state.meshes[name];
      const home = state.home[name];
      if (!home) continue;
      const offset = EXPLODED[name] || EXPLODED.case;
      mesh.position.lerpVectors(home.clone().add(offset), home, t);
    }
    const screenMesh = state.meshes.screen;
    if (screenMesh) {
      screenMesh.userData.halo.material.opacity = 0.22 * clamp01((state.progress - 0.55) / 0.45);
    }
    const narrow = camera.aspect < 0.72;
    const camZ = (-142 + 24 * t) * (narrow ? 1.42 : 1);
    const camY = 50 - 32 * t + (narrow ? 6 : 0);
    const camX = narrow ? 0 : Math.sin(t * 0.7) * 30;
    camera.position.set(camX, camY, camZ);
    controls.target.copy(state.screenWorld);
    camera.lookAt(state.screenWorld);
  }

  function setCameraView(view = "front") {
    if (!state.ready) return;
    const basis = state.screenBasis;
    const target = state.screenWorld.clone();
    const specs = {
      front: { normal: 172, right: 18, up: 14 },
      screen: { normal: 68, right: 0, up: 4 },
      side: { normal: 72, right: 136, up: 14 },
    };
    const narrow = camera.aspect < 0.72;
    const base = specs[view] || specs.front;
    const spec = {
      normal: base.normal * (narrow ? 1.42 : 1),
      right: base.right * (narrow ? 0.35 : 1),
      up: base.up + (narrow ? 5 : 0),
    };
    const nextPosition = target.clone()
      .addScaledVector(basis.normal, spec.normal)
      .addScaledVector(basis.right, spec.right)
      .addScaledVector(basis.up, spec.up);
    state.cameraTween = {
      start: performance.now(),
      duration: 520,
      fromPosition: camera.position.clone(),
      toPosition: nextPosition,
      fromTarget: controls.target.clone(),
      toTarget: target,
    };
  }

  function resize() {
    const width = Math.max(320, container.clientWidth || 320);
    const height = Math.max(320, container.clientHeight || 320);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    renderer.setSize(width, height);
    if (state.ready && !state.cameraTween) setAssemblyProgress(state.progress);
  }

  function animate() {
    requestAnimationFrame(animate);
    updateCameraTween();
    controls.update();
    renderer.render(scene, camera);
  }

  function updateCameraTween() {
    if (!state.cameraTween) return;
    const elapsed = performance.now() - state.cameraTween.start;
    const t = easeInOutCubic(clamp01(elapsed / state.cameraTween.duration));
    camera.position.lerpVectors(state.cameraTween.fromPosition, state.cameraTween.toPosition, t);
    controls.target.lerpVectors(state.cameraTween.fromTarget, state.cameraTween.toTarget, t);
    if (t >= 1) state.cameraTween = null;
  }

  function orientScreenNormalToCamera() {
    const toCamera = camera.position.clone().sub(state.screenWorld);
    if (state.screenBasis.normal.dot(toCamera) < 0) {
      state.screenBasis.normal.multiplyScalar(-1);
      state.screenBasis.right.multiplyScalar(-1);
    }
  }

  return { updateTexture, setAssemblyProgress, setCameraView, get ready() { return state.ready; } };
}

async function loadModels(group, texture, state) {
  const loader = new STLLoader();
  const dark = new THREE.MeshPhysicalMaterial({ color: 0x252827, roughness: 0.68, metalness: 0.08, clearcoat: 0.18, clearcoatRoughness: 0.55 });
  const green = new THREE.MeshPhysicalMaterial({ color: 0x00f068, roughness: 0.48, metalness: 0.06, emissive: 0x00f068, emissiveIntensity: 0.28 });

  const entries = await Promise.all(Object.entries(MODEL_FILES).map(async ([name, file]) => [name, await loadSTL(loader, file)]));
  for (const [name, geo] of entries) {
    geo.computeVertexNormals();
    const material = name === "logo" ? green : dark;
    const mesh = new THREE.Mesh(geo, material);
    state.meshes[name] = mesh;
    group.add(mesh);
  }

  const combined = new THREE.Box3().setFromObject(group);
  const center = new THREE.Vector3();
  const size = new THREE.Vector3();
  combined.getCenter(center);
  combined.getSize(size);
  group.children.forEach((child) => child.position.sub(center));
  group.scale.setScalar(150 / Math.max(size.x, size.y, size.z));
  group.rotation.x = -Math.PI / 2;

  for (const [name, mesh] of Object.entries(state.meshes)) {
    state.home[name] = mesh.position.clone();
  }

  attachScreen(state.meshes.case, group, texture, state);
}

function attachScreen(caseMesh, group, texture, state) {
  const assembly = new THREE.Group();
  const halo = makeHalo();
  assembly.add(halo);
  const backplate = new THREE.Mesh(
    new THREE.PlaneGeometry(67.5, 67.5),
    new THREE.MeshBasicMaterial({ color: 0x020403, side: THREE.FrontSide, toneMapped: false }),
  );
  backplate.position.z = -0.015;
  assembly.add(backplate);

  const screen = new THREE.Mesh(
    new THREE.PlaneGeometry(64, 64),
    new THREE.MeshBasicMaterial({
      map: texture,
      transparent: false,
      side: THREE.FrontSide,
      toneMapped: false,
      depthTest: true,
      depthWrite: true,
      polygonOffset: true,
      polygonOffsetFactor: -1,
      polygonOffsetUnits: -1,
    }),
  );
  screen.position.z = 0.018;
  screen.userData.halo = halo;
  assembly.add(screen);
  const glass = new THREE.Mesh(
    new THREE.PlaneGeometry(64, 64),
    new THREE.MeshBasicMaterial({
      color: 0xffffff,
      transparent: true,
      opacity: 0.045,
      depthTest: true,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
      side: THREE.FrontSide,
      toneMapped: false,
    }),
  );
  glass.position.z = 0.028;
  assembly.add(glass);
  caseMesh.add(assembly);
  state.meshes.screen = screen;

  caseMesh.geometry.computeBoundingBox();
  const box = caseMesh.geometry.boundingBox;
  const center = new THREE.Vector3();
  box.getCenter(center);

  // The physical screen recess in the STL is tilted, so the canvas must share
  // that local plane instead of using the case bounding-box front.
  const frontNormal = new THREE.Vector3(0, 1, -SCREEN_PLANE_SLOPE_Z).normalize();
  const planeUp = new THREE.Vector3(0, 0, 1).projectOnPlane(frontNormal).normalize();
  const planeRight = new THREE.Vector3().crossVectors(planeUp, frontNormal).normalize();
  const basis = new THREE.Matrix4().makeBasis(planeRight, planeUp, frontNormal);
  const screenCenter = new THREE.Vector3(
    center.x,
    SCREEN_PLANE_SLOPE_Z * center.z + SCREEN_PLANE_INTERCEPT,
    center.z,
  );
  assembly.position.copy(screenCenter).addScaledVector(frontNormal, SCREEN_PLANE_LIFT);
  assembly.quaternion.setFromRotationMatrix(basis);

  group.updateMatrixWorld(true);
  screen.getWorldPosition(state.screenWorld);
  const worldQ = new THREE.Quaternion();
  assembly.getWorldQuaternion(worldQ);
  state.screenBasis.normal.copy(new THREE.Vector3(0, 0, 1).applyQuaternion(worldQ).normalize());
  state.screenBasis.right.copy(new THREE.Vector3(1, 0, 0).applyQuaternion(worldQ).normalize());
  state.screenBasis.up.copy(new THREE.Vector3(0, 1, 0).applyQuaternion(worldQ).normalize());
}

function makeHalo() {
  const canvas = document.createElement("canvas");
  canvas.width = 256;
  canvas.height = 256;
  const ctx = canvas.getContext("2d");
  const gradient = ctx.createRadialGradient(128, 128, 36, 128, 128, 128);
  gradient.addColorStop(0, "rgba(0,240,104,0.18)");
  gradient.addColorStop(0.48, "rgba(0,240,104,0.05)");
  gradient.addColorStop(1, "rgba(0,240,104,0)");
  ctx.fillStyle = gradient;
  ctx.fillRect(0, 0, 256, 256);
  const tex = new THREE.CanvasTexture(canvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  const mat = new THREE.MeshBasicMaterial({ map: tex, transparent: true, opacity: 0.32, blending: THREE.AdditiveBlending, depthWrite: false });
  const mesh = new THREE.Mesh(new THREE.PlaneGeometry(74, 74), mat);
  mesh.position.z = -0.035;
  return mesh;
}

function loadSTL(loader, file) {
  return new Promise((resolve, reject) => loader.load(MODEL_BASE + file, resolve, undefined, reject));
}

function addLight(scene, color, intensity, x, y, z) {
  const light = new THREE.DirectionalLight(color, intensity);
  light.position.set(x, y, z);
  scene.add(light);
}

function easeInOutCubic(t) {
  return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
}

function clamp01(value) {
  return Math.max(0, Math.min(1, Number(value) || 0));
}
