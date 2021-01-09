#include "SiPMSensor.h"
#ifdef __AVX2__
#include <immintrin.h>
#endif

#include "SiPMProperties.h"

namespace sipm {

// Public
// Ctor
SiPMSensor::SiPMSensor(const SiPMProperties& aProperty) noexcept {
  m_Properties = aProperty;
  m_SignalShape = signalShape();
  m_Signal.setSampling(m_Properties.sampling());
}

void SiPMSensor::setProperty(const std::string& prop, const double val) {
  m_Properties.setProperty(prop, val);
  m_SignalShape = signalShape();
  m_Signal.setSampling(m_Properties.sampling());
}

void SiPMSensor::setProperties(const std::vector<std::string>& props,
                               const std::vector<double>& vals) {
  for (uint32_t i = 0; i < props.size(); ++i) {
    m_Properties.setProperty(props[i], vals[i]);
  }
}

const std::vector<double> SiPMSensor::signalShape() const {
  std::vector<double> lSignalShape(m_Properties.nSignalPoints());

  double sampling = m_Properties.sampling();
  double tr = m_Properties.risingTime() / sampling;
  double tff = m_Properties.fallingTimeFast() / sampling;

  if (m_Properties.hasSlowComponent()) {
    double tfs = m_Properties.fallingTimeSlow() / sampling;
    double slf = m_Properties.slowComponentFraction();

    for (int32_t i = 0; i < m_Properties.nSignalPoints(); ++i) {
      lSignalShape[i] =
        (1 - slf) * exp(-i / tff) + slf * exp(-i / tfs) - exp(-i / tr);
    }
  } else {
    for (int32_t i = 0; i < m_Properties.nSignalPoints(); ++i) {
      lSignalShape[i] = exp(-i / tff) - exp(-i / tr);
    }
  }

  const double peak =
    *std::max_element(lSignalShape.begin(), lSignalShape.end());

  for (uint32_t i = 0; i < m_Properties.nSignalPoints(); ++i) {
    lSignalShape[i] /= peak;
  }

  return lSignalShape;
}

void SiPMSensor::addPhoton(const double aTime, const double aWavelength) {
  m_PhotonTimes.emplace_back(aTime);
  m_PhotonWavelengths.emplace_back(aWavelength);
}

void SiPMSensor::addPhotons(const std::vector<double>& aTimes,
                            const std::vector<double>& aWavelengths) {
  resetState();
  m_PhotonTimes = aTimes;
  m_PhotonWavelengths = aWavelengths;
}

// Private
const double SiPMSensor::evaluatePde(const double aPhotonWavelength) const {
  return 0;
}

const bool SiPMSensor::isInSensor(const int32_t r, const int32_t c) const {
  const int32_t nSideCells = m_Properties.nSideCells();
  return (r * c > 0) | (r < nSideCells) | (c < nSideCells);
}

const std::pair<int32_t, int32_t> SiPMSensor::hitCell() const {
  int32_t row, col;
  switch (m_Properties.hitDistribution()) {
  case (SiPMProperties::HitDistribution::kUniform):
    row = m_rng.randInteger(m_Properties.nSideCells());
    col = m_rng.randInteger(m_Properties.nSideCells());
    break;
  }

  return std::make_pair(row, col);
}

void SiPMSensor::addPhotoelectrons() {
  const uint32_t nPhotons = m_PhotonTimes.size();
  const double pde = m_Properties.pde();
  m_Hits.reserve(nPhotons);

  switch (m_Properties.hasPde()) {
  case (SiPMProperties::PdeType::kNoPde):
    // Add all photons
    for (uint32_t i = 0; i < nPhotons; ++i) {
      std::pair<int32_t, int32_t> position = hitCell();

      m_Hits.emplace_back(m_PhotonTimes[i], 1, position.first, position.second,
                          SiPMHit::HitType::kPhotoelectron);

      m_nTotalHits++;
      m_nPe++;
    }
    break;

  case (SiPMProperties::PdeType::kSimplePde):
    // Simple pde
    for (uint32_t i = 0; i < nPhotons; ++i) {
      if (isDetected(pde)) {
        std::pair<int32_t, int32_t> position = hitCell();

        m_Hits.emplace_back(m_PhotonTimes[i], 1, position.first,
                            position.second, SiPMHit::HitType::kPhotoelectron);

        m_nTotalHits++;
        m_nPe++;
      }
    }
    break;

  case (SiPMProperties::PdeType::kSpectrumPde):
    // Evaluate pde based on wavelength
    for (uint32_t i = 0; i < nPhotons; ++i) {
      if (isDetected(evaluatePde(m_PhotonWavelengths[i]))) {
        std::pair<int32_t, int32_t> position = hitCell();

        m_Hits.emplace_back(m_PhotonTimes[i], 1, position.first,
                            position.second, SiPMHit::HitType::kPhotoelectron);

        m_nTotalHits++;
        m_nPe++;
      }
    }
    break;
  } /* SWITCH */
}

void SiPMSensor::addDcrEvents() {
  const double signalLength = m_Properties.signalLength();
  const double meanDcr = 1e9 / m_Properties.dcr();
  const int32_t nSideCells = m_Properties.nSideCells();

  double last = -100;

  while (last < signalLength) {
    last += m_rng.randExponential(meanDcr);
    if ((last > 0) && (last < signalLength)) {
      int32_t row = m_rng.randInteger(nSideCells);
      int32_t col = m_rng.randInteger(nSideCells);

      m_Hits.emplace_back(last, 1, row, col, SiPMHit::HitType::kDarkCount);

      m_nTotalHits++;
      m_nDcr++;
    }
  }
}

void SiPMSensor::addXtEvents() {
  const double xt = m_Properties.xt();

  uint32_t currentCellIdx = 0;
  while (currentCellIdx < m_nTotalHits) {
    SiPMHit* hit = &m_Hits[currentCellIdx];
    currentCellIdx++;
    uint32_t nxt = m_rng.randPoisson(xt);

    if (nxt == 0) { continue; }
    int32_t xtGeneratorRow = hit->row();
    int32_t xtGeneratorCol = hit->col();
    double xtTime = hit->time();

    for (uint32_t j = 0; j < nxt; ++j) {
      int32_t rowAdd, colAdd;
      do {
        rowAdd = m_rng.randInteger(2) - 1;
        colAdd = m_rng.randInteger(2) - 1;
      } while (rowAdd + colAdd == 0);
      int32_t xtRow = xtGeneratorRow + rowAdd;
      int32_t xtCol = xtGeneratorCol + colAdd;

      if (isInSensor(xtRow, xtCol)) {
        m_Hits.emplace_back(xtTime, 1, xtRow, xtCol,
                            SiPMHit::HitType::kOpticalCrosstalk);
        ++m_nTotalHits;
        ++m_nXt;
      }
    } /* FOR XT */
  }   /* WHILE HIT */
}

void SiPMSensor::addApEvents() {
  const double ap = m_Properties.ap();
  const double tauApFast = m_Properties.tauApFast();
  const double tauApSlow = m_Properties.tauApSlow();
  const double signalLength = m_Properties.signalLength();
  const double recoveryTime = m_Properties.recoveryTime();
  const double slowFraction = m_Properties.apSlowFraction();

  // Cannot use iterator based loop becouse of reallocation
  uint32_t currentCellIdx = 0;
  while (currentCellIdx < m_nTotalHits) {
    SiPMHit* hit = &m_Hits[currentCellIdx];
    currentCellIdx++;

    int32_t nap = m_rng.randPoisson(ap);

    if (nap == 0) { continue; }
    double apGeneratorTime = hit->time();

    // Choose fast or slow ap
    for (uint32_t j = 0; j < nap; ++j) {
      double apDelay;
      if (m_rng.Rand() < slowFraction) {
        apDelay = m_rng.randExponential(tauApSlow);
      } else {
        apDelay = m_rng.randExponential(tauApFast);
      }

      // If ap event is in signal window
      if (apGeneratorTime + apDelay < signalLength) {
        double apAmplitude =
          hit->amplitude() * (1 - exp(-apDelay / recoveryTime));

        m_Hits.emplace_back(apGeneratorTime + apDelay, apAmplitude, hit->row(),
                            hit->col(), SiPMHit::HitType::kAfterPulse);

        m_nTotalHits++;
        m_nAp++;
      }
    } /* FOR AP */
  }   /* WHILE HIT */
}

const std::pair<std::vector<uint32_t>, std::unordered_set<uint32_t>>
SiPMSensor::getUniqueId() const {

  std::vector<uint32_t> cellId;
  cellId.reserve(m_Hits.size());
  for (auto hit = m_Hits.begin(); hit != m_Hits.end(); ++hit) {
    cellId.emplace_back(hit->id());
  }
  std::unordered_set<uint32_t> uniqueCellId(cellId.begin(), cellId.end());

  return std::make_pair(cellId, uniqueCellId);
}

void SiPMSensor::calculateSignalAmplitudes() {
  sortHits();
  const auto unique = getUniqueId();
  const std::vector<uint32_t> cellId = unique.first;
  const std::unordered_set<uint32_t> uniqueCellId = unique.second;
  const double cellRecovery = m_Properties.recoveryTime();

  for (auto itr = uniqueCellId.begin(); itr != uniqueCellId.end(); ++itr) {
    // If cell hitted more than once
    if (std::count(cellId.begin(), cellId.end(), *itr) > 1) {
      double previousHitTime = 0;
      for (auto hit = m_Hits.begin(); hit != m_Hits.end(); ++hit) {
        if (hit->id() == *itr) {
          if (previousHitTime == 0) {
            previousHitTime = hit->time();
          } else {
            double delay = hit->time() - previousHitTime;
            hit->amplitude() = 1 - exp(-delay / cellRecovery);
            previousHitTime = hit->time();
          }
        }
      }
    }
  }
}

void SiPMSensor::generateSignal() {
  const uint32_t nSignalPoints = m_Properties.nSignalPoints();
  const double sampling = m_Properties.sampling();

  m_Signal = m_rng.randGaussian(0, m_Properties.snrLinear(), nSignalPoints);

  for (auto hit = m_Hits.begin(); hit != m_Hits.cend(); ++hit) {
    const uint32_t time = hit->time() / sampling;
    const double amplitude =
      hit->amplitude() * m_rng.randGaussian(1, m_Properties.ccgv());

#ifdef __AVX2__
    uint32_t i = 0;
    const __m256d __amplitude = _mm256_set1_pd(amplitude);
    for (i = time; i < nSignalPoints - 4; i += 4) {
      __m256d __signal = _mm256_loadu_pd(&m_Signal[i]);
      __m256d __shape = _mm256_loadu_pd(&m_SignalShape[i - time]);
      __signal = _mm256_fmadd_pd(__shape, __amplitude, __signal); // s += sh * a
      _mm256_storeu_pd(&m_Signal[i], __signal);
    }
    for (i; i < nSignalPoints; ++i) {
      m_Signal[i] += m_SignalShape[i - time] * amplitude;
    }
#else
    for (uint32_t i = time; i < nSignalPoints; ++i) {
      m_Signal[i] += m_SignalShape[i - time] * amplitude;
    }
#endif
  }
}

void SiPMSensor::runEvent() {
  if (m_Properties.hasDcr()) { addDcrEvents(); }
  addPhotoelectrons();
  if (m_Properties.hasXt()) { addXtEvents(); }
  calculateSignalAmplitudes();
  if (m_Properties.hasAp()) { addApEvents(); }
  generateSignal();
}

void SiPMSensor::resetState() {
  m_nTotalHits = 0;
  m_nPe = 0;
  m_nDcr = 0;
  m_nXt = 0;
  m_nAp = 0;

  m_Hits.clear();
  m_PhotonTimes.clear();
  m_PhotonWavelengths.clear();
}

} // namespace sipm
