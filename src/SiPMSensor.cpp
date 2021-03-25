#include "SiPMSensor.h"
#ifdef __AVX2__
#include <immintrin.h>
#endif
#include <algorithm>
#include <math.h>

#include "SiPMProperties.h"

namespace sipm {

SiPMSensor::SiPMSensor(const SiPMProperties& aProperty) noexcept {
  m_Properties = aProperty;
  m_Signal.setSampling(m_Properties.sampling());
  m_SignalShape = signalShape();
}

void SiPMSensor::setProperty(const std::string& prop, const double val) {
  m_Properties.setProperty(prop, val);
  m_Signal.setSampling(m_Properties.sampling());
  m_SignalShape = signalShape();
}

void SiPMSensor::setProperties(const SiPMProperties& x) {
  m_Properties = x;
  m_Signal.setSampling(m_Properties.sampling());
  m_SignalShape = signalShape();
}

void SiPMSensor::setPrecisionLevel(const PrecisionLevel x) { m_PrecisionLevel = x; }

void SiPMSensor::addPhoton(const double aTime) { m_PhotonTimes.emplace_back(aTime); }

void SiPMSensor::addPhoton(const double aTime, const double aWavelength) {
  m_PhotonTimes.emplace_back(aTime);
  m_PhotonWavelengths.emplace_back(aWavelength);
}

void SiPMSensor::addPhotons(const std::vector<double>& aTimes) { m_PhotonTimes = aTimes; }

void SiPMSensor::addPhotons(const std::vector<double>& aTimes, const std::vector<double>& aWavelengths) {
  m_PhotonTimes = aTimes;
  m_PhotonWavelengths = aWavelengths;
}

void SiPMSensor::runEvent() {
  if (m_Properties.hasDcr()) {
    addDcrEvents();
  }
  addPhotoelectrons();
  if (m_Properties.hasXt()) {
    addXtEvents();
  }
  calculateSignalAmplitudes();
  if (m_Properties.hasAp()) {
    addApEvents();
  }
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
  m_Signal.clear();
}

std::vector<double> SiPMSensor::signalShape() const {
  const int32_t nSignalPoints = m_Properties.nSignalPoints();
  const double sampling = m_Properties.sampling();
  const double tr = m_Properties.risingTime() / sampling;
  const double tff = m_Properties.fallingTimeFast() / sampling;
  std::vector<double> lSignalShape(nSignalPoints);

  if (m_Properties.hasSlowComponent()) {
    const double tfs = m_Properties.fallingTimeSlow() / sampling;
    const double slf = m_Properties.slowComponentFraction();

    for (int32_t i = 0; i < nSignalPoints; ++i) {
      lSignalShape[i] = (1 - slf) * exp(-i / tff) + slf * exp(-i / tfs) - exp(-i / tr);
    }
  } else {
    for (int32_t i = 0; i < nSignalPoints; ++i) {
      lSignalShape[i] = exp(-i / tff) - exp(-i / tr);
    }
  }

  const double peak = *std::max_element(lSignalShape.begin(), lSignalShape.end());

  for (int32_t i = 0; i < nSignalPoints; ++i) {
    lSignalShape[i] /= peak;
  }

  return lSignalShape;
}

double SiPMSensor::evaluatePde(const double x) const {
  // Linear interpolation
  const std::map<double, double> pde = m_Properties.pdeSpectrum();
  auto it1 = pde.upper_bound(x);
  if(it1 == pde.end()){ --it1;}
  if(it1 == pde.begin()){ ++it1; }

  auto it0 = it1; --it0;

  const double weight = (x - it0->first) / (it1->first - it0->first);
  return (weight * it1->second) + ((1-weight) * it0->second);
}


bool SiPMSensor::isInSensor(const int32_t r, const int32_t c) const {
  const int32_t nSideCells = m_Properties.nSideCells() - 1;
  return (r >= 0) && (c >= 0) && (r < nSideCells) && (c < nSideCells);
}


std::pair<int32_t, int32_t> SiPMSensor::hitCell() const {
  int32_t row, col;
  double x, y;
  const int32_t nSideCells = m_Properties.nSideCells() - 1;   // index start from 0. nSidecels = 9 gives 10 cells

  switch (m_Properties.hitDistribution()) {

    // Uniform on the sensor
    case (SiPMProperties::HitDistribution::kUniform):
      row = m_rng.randInteger(nSideCells);
      col = m_rng.randInteger(nSideCells);
      return std::make_pair(row, col);

    // Circle centered in sensor 95% probability in circle
    case (SiPMProperties::HitDistribution::kCircle):
      if (m_rng.Rand() < 0.95) {  // In circle
        do {
          x = m_rng.Rand() * 2 - 1; // x in [-1,1]
          y = m_rng.Rand() * 2 - 1; // y in [-1,1]
        } while (x * x + y * y > 1);    // if in unitary circle
        row = (x + 1) * (m_Properties.nSideCells() / 2);
        col = (y + 1) * (m_Properties.nSideCells() / 2);
      } else {  // Outside
        do {
          x = m_rng.Rand() * 2 - 1; // x in [-1,1]
          y = m_rng.Rand() * 2 - 1; // y in [-1,1]
        } while (x * x + y * y < 1);    // if outside in unitary circle
        row = (x + 1) * (m_Properties.nSideCells() / 2);
        col = (y + 1) * (m_Properties.nSideCells() / 2);
      }
      return std::make_pair(row, col);

    case (SiPMProperties::HitDistribution::kGaussian):
      x = m_rng.randGaussian(0,1);
      y = m_rng.randGaussian(0,1);

      if (abs(x) < 3 && abs(y) < 3){  // 95% of samples (3 sigmas)
        row = (x + 3) * (m_Properties.nSideCells()/6);
        col = (y + 3) * (m_Properties.nSideCells()/6);
      } else {
        row = m_rng.randInteger(nSideCells);
        col = m_rng.randInteger(nSideCells);
      }
      return std::make_pair(row,col);

    default:
      row = m_rng.randInteger(nSideCells);
      col = m_rng.randInteger(nSideCells);
      return std::make_pair(row, col);
    }
}


std::vector<uint32_t> SiPMSensor::getCellIds() const {
  std::vector<uint32_t> cellId;
  cellId.reserve(m_Hits.size());
  for (auto hit = m_Hits.begin(); hit != m_Hits.end(); ++hit) {
    cellId.emplace_back(hit->id());
  }

  return cellId;
}


void SiPMSensor::addDcrEvents() {
  const double signalLength = m_Properties.signalLength();
  const double meanDcr = 1e9 / m_Properties.dcr();
  const int32_t nSideCells = m_Properties.nSideCells() - 1;

  // Start generation "before" the signal window
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


void SiPMSensor::addPhotoelectrons() {
  const uint32_t nPhotons = m_PhotonTimes.size();
  const double pde = m_Properties.pde();
  m_Hits.reserve(nPhotons);

  switch (m_Properties.pdeType()) {
  case (SiPMProperties::PdeType::kNoPde):
    // Add all photons
    for (uint32_t i = 0; i < nPhotons; ++i) {
      std::pair<int32_t, int32_t> position = hitCell();

      m_Hits.emplace_back(m_PhotonTimes[i], 1, position.first, position.second, SiPMHit::HitType::kPhotoelectron);

      m_nTotalHits++;
      m_nPe++;
    }
    break;

  case (SiPMProperties::PdeType::kSimplePde):
    // Simple pde
    for (uint32_t i = 0; i < nPhotons; ++i) {
      if (isDetected(pde)) {
        std::pair<int32_t, int32_t> position = hitCell();

        m_Hits.emplace_back(m_PhotonTimes[i], 1, position.first, position.second, SiPMHit::HitType::kPhotoelectron);

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

        m_Hits.emplace_back(m_PhotonTimes[i], 1, position.first, position.second, SiPMHit::HitType::kPhotoelectron);

        m_nTotalHits++;
        m_nPe++;
      }
    }
    break;
  } /* SWITCH */
}


void SiPMSensor::addXtEvents() {
  const double xt = m_Properties.xt();

  // Use while becouse number of hits increases in loop
  uint32_t currentCellIdx = 0;
  while (currentCellIdx < m_nTotalHits) {
    SiPMHit* hit = &m_Hits[currentCellIdx];
    currentCellIdx++;

    double test = m_rng.Rand();
    // Poisson process algorithm
    while (test > exp(-xt)) {
      int32_t xtGeneratorRow = hit->row();
      int32_t xtGeneratorCol = hit->col();
      double xtTime = hit->time();

      int32_t rowAdd, colAdd;
      do {
        rowAdd = m_rng.randInteger(2) - 1;
        colAdd = m_rng.randInteger(2) - 1;
      } while (rowAdd + colAdd == 0);
      int32_t xtRow = xtGeneratorRow + rowAdd;
      int32_t xtCol = xtGeneratorCol + colAdd;

      if (isInSensor(xtRow, xtCol)) {
        m_Hits.emplace_back(xtTime, 1, xtRow, xtCol, SiPMHit::HitType::kOpticalCrosstalk);
        ++m_nTotalHits;
        ++m_nXt;
      }
      test *= m_rng.Rand();
    } /* WHILE TEST < XT */
  }   /* WHILE HIT */
}


void SiPMSensor::addApEvents() {
  const double ap = m_Properties.ap();
  const double tauApFast = m_Properties.tauApFast();
  const double tauApSlow = m_Properties.tauApSlow();
  const double signalLength = m_Properties.signalLength();
  const double recoveryTime = m_Properties.recoveryTime();
  const double slowFraction = m_Properties.apSlowFraction();

  // Use while becouse number of hits increases in loop
  uint32_t currentCellIdx = 0;
  while (currentCellIdx < m_nTotalHits) {
    SiPMHit* hit = &m_Hits[currentCellIdx];
    currentCellIdx++;

    double test = m_rng.Rand();
    double apGeneratorTime = hit->time();
    // Poisson process algorithm
    while (test > exp(-ap)) {
      double apDelay;
      if (m_rng.Rand() < slowFraction) {
        apDelay = m_rng.randExponential(tauApSlow);
      } else {
        apDelay = m_rng.randExponential(tauApFast);
      }

      // If ap event is in signal window
      if (apGeneratorTime + apDelay < signalLength) {
        double apAmplitude = hit->amplitude() * (1 - exp(-apDelay / recoveryTime));

        m_Hits.emplace_back(apGeneratorTime + apDelay, apAmplitude, hit->row(), hit->col(),
                            SiPMHit::HitType::kAfterPulse);

        m_nTotalHits++;
        m_nAp++;
      }
      test = m_rng.Rand();
    } /* WHILE TEST < AP */
  }   /* WHILE HIT */
}


void SiPMSensor::calculateSignalAmplitudes() {
  sortHits();
  const std::vector<uint32_t> cellId = getCellIds();
  const std::unordered_set<uint32_t> uniqueCellId(cellId.begin(), cellId.end());
  const double tauRecovery = 1 / m_Properties.recoveryTime();

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
            hit->amplitude() = 1 - exp(-delay * tauRecovery);
            previousHitTime = hit->time();
          }
        }
      }
    }
  }
}


#ifdef __AVX2__
void SiPMSensor::generateSignal() {
  const uint32_t nSignalPoints = m_Properties.nSignalPoints();
  const double sampling = m_Properties.sampling();

  m_Signal = m_rng.randGaussian(0, m_Properties.snrLinear(), nSignalPoints);

  for (auto hit = m_Hits.begin(); hit != m_Hits.end(); ++hit) {
    const int32_t time = hit->time() / sampling;
    const double amplitude = hit->amplitude() * m_rng.randGaussian(1, m_Properties.ccgv());

    const __m256d __amplitude = _mm256_set1_pd(amplitude);

    for (uint32_t i = time; i < nSignalPoints - 4; i += 4) {
      __m256d __signal = _mm256_loadu_pd(&m_Signal[i]);
      __m256d __shape = _mm256_loadu_pd(&m_SignalShape[i - time]);
      __signal = _mm256_fmadd_pd(__shape, __amplitude, __signal);
      _mm256_storeu_pd(&m_Signal[i], __signal);
    }
    for (uint32_t i = nSignalPoints; i < nSignalPoints; ++i) {
      m_Signal[i] += m_SignalShape[i - time] * amplitude;
    }
  }
}
#else
void SiPMSensor::generateSignal() {
  const uint32_t nSignalPoints = m_Properties.nSignalPoints();
  const double sampling = m_Properties.sampling();

  m_Signal = m_rng.randGaussian(0, m_Properties.snrLinear(), nSignalPoints);

  for (auto hit = m_Hits.begin(); hit != m_Hits.end(); ++hit) {
    const int32_t time = hit->time() / sampling;
    const double amplitude = hit->amplitude() * m_rng.randGaussian(1, m_Properties.ccgv());

    for (uint32_t i = time; i < nSignalPoints; ++i) {
      m_Signal[i] += m_SignalShape[i - time] * amplitude;
    }
  }
}
#endif

}  // namespace sipm
